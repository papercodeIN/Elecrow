#include <PCA9557.h>
#include <lvgl.h>
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <SPI.h>
#include <WiFi.h>
#include <Preferences.h>
#include <WebServer.h>
#include <time.h>
#include <esp_sntp.h>
#include "secrets.h"
#include "ui.h"

class LGFX : public lgfx::LGFX_Device
{
public:
  lgfx::Bus_RGB     _bus_instance;
  lgfx::Panel_RGB   _panel_instance;

  LGFX(void)
  {
    {
      auto cfg = _bus_instance.config();
      cfg.panel = &_panel_instance;

      cfg.pin_d0  = GPIO_NUM_15; // B0
      cfg.pin_d1  = GPIO_NUM_7;  // B1
      cfg.pin_d2  = GPIO_NUM_6;  // B2
      cfg.pin_d3  = GPIO_NUM_5;  // B3
      cfg.pin_d4  = GPIO_NUM_4;  // B4

      cfg.pin_d5  = GPIO_NUM_9;  // G0
      cfg.pin_d6  = GPIO_NUM_46; // G1
      cfg.pin_d7  = GPIO_NUM_3;  // G2
      cfg.pin_d8  = GPIO_NUM_8;  // G3
      cfg.pin_d9  = GPIO_NUM_16; // G4
      cfg.pin_d10 = GPIO_NUM_1;  // G5

      cfg.pin_d11 = GPIO_NUM_14; // R0
      cfg.pin_d12 = GPIO_NUM_21; // R1
      cfg.pin_d13 = GPIO_NUM_47; // R2
      cfg.pin_d14 = GPIO_NUM_48; // R3
      cfg.pin_d15 = GPIO_NUM_45; // R4

      cfg.pin_henable = GPIO_NUM_41;
      cfg.pin_vsync   = GPIO_NUM_40;
      cfg.pin_hsync   = GPIO_NUM_39;
      cfg.pin_pclk    = GPIO_NUM_0;
      cfg.freq_write  = 15000000;

      cfg.hsync_polarity    = 0;
      cfg.hsync_front_porch = 40;
      cfg.hsync_pulse_width = 48;
      cfg.hsync_back_porch  = 40;

      cfg.vsync_polarity    = 0;
      cfg.vsync_front_porch = 1;
      cfg.vsync_pulse_width = 31;
      cfg.vsync_back_porch  = 13;

      cfg.pclk_active_neg   = 1;
      cfg.de_idle_high      = 0;
      cfg.pclk_idle_high    = 0;

      _bus_instance.config(cfg);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.memory_width  = 800;
      cfg.memory_height = 480;
      cfg.panel_width  = 800;
      cfg.panel_height = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      _panel_instance.config(cfg);
    }
    _panel_instance.setBus(&_bus_instance);
    setPanel(&_panel_instance);
  }
};

LGFX lcd;
PCA9557 Out;
int brightness = 255; // Brightness level (0-255)

// WiFi credentials (loaded at runtime from NVS or secrets.h)
String wifiSSID = "";
String wifiPass = "";
Preferences wifiPrefs;
WebServer configServer(80);

// ---- CHRONO network/time state machine (ported from the 2.8" reference) ----
const char* NTP_SERVERS[] = {"time.google.com", "pool.ntp.org", "time.cloudflare.com"};
const long IST_OFFSET_SECONDS = 19800; // UTC+5:30
constexpr uint32_t WIFI_BACKOFF_MIN_MS = 2000;
constexpr uint32_t WIFI_BACKOFF_MAX_MS = 60000;
constexpr uint32_t NTP_REQUEST_TIMEOUT_MS = 20000;
constexpr uint32_t NTP_RETRY_MS = 30000;
constexpr uint32_t NTP_RESYNC_MS = 6UL * 60UL * 60UL * 1000UL;
constexpr time_t VALID_TIME_EPOCH = 1704067200; // 2024-01-01

// UK time uses the system TZ (Europe/London style TZ string), so DST is exact
const char* UK_TZ_SPEC = "GMT0BST,M3.5.0/1,M10.5.0/2";

const char* dayNames[] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
const char* monthNames[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

enum SyncState : uint8_t {
  ConnectingWifi,
  SyncingTime,
  Synced,
  SyncFailed,
  OfflineCached,
  OfflineNoTime
};

bool ntpSynced = false;
bool wifiWasConnected = false;
bool hasCachedTime = false;
uint32_t wifiRetryDelayMs = WIFI_BACKOFF_MIN_MS;
uint32_t nextWifiAttemptMs = 0;
uint32_t nextNtpAttemptMs = 0;
uint32_t lastNtpSyncMs = 0;
time_t lastNtpSyncEpoch = 0;
uint32_t ntpRequestStartedMs = 0;
bool ntpWasEverSynced = false;
volatile bool ntpSyncEvent = false;
SyncState syncState = ConnectingWifi;
String ipAddress = "NO WIFI";

// Interactive features
int currentTheme = 0;
bool use24HourFormat = false; // Default to 12-hour format
unsigned long lastInteractionTime = 0;
bool buttonsVisible = true;

//UI
#define TFT_BL 2
SPIClass& spi = SPI;
#include "touch.h"

/* Change to your screen resolution */
static uint32_t screenWidth;
static uint32_t screenHeight;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t disp_draw_buf[800 * 480 / 15];
static lv_disp_drv_t disp_drv;

/* Display flushing */
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

#if (LV_COLOR_16_SWAP != 0)
 lcd.pushImageDMA(area->x1, area->y1, w, h,(lgfx::rgb565_t*)&color_p->full);
#else
  lcd.pushImageDMA(area->x1, area->y1, w, h,(lgfx::rgb565_t*)&color_p->full);
#endif

  lv_disp_flush_ready(disp);
}

void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data)
{
  if (touch_has_signal())
  {
    if (touch_touched())
    {
      data->state = LV_INDEV_STATE_PR;
      data->point.x = touch_last_x;
      data->point.y = touch_last_y;
    }
    else if (touch_released())
    {
      data->state = LV_INDEV_STATE_REL;
    }
  }
  else
  {
    data->state = LV_INDEV_STATE_REL;
  }
  delay(15);
}

void loadWiFiCredentials() {
  wifiPrefs.begin("wifi", false); // read/write so the namespace is created on first boot
  wifiSSID = wifiPrefs.getString("ssid", "");
  wifiPass = wifiPrefs.getString("pass", "");
  wifiPrefs.end();
  if (wifiSSID.length() == 0) {
    if (strlen(WIFI_SSID) > 0 && strcmp(WIFI_SSID, "YOUR_WIFI_SSID") != 0) {
      wifiSSID = WIFI_SSID;
      wifiPass = WIFI_PASSWORD;
    }
  }
}

void handleConfigRoot() {
  String html = "<html><body style='background:#020B1E;color:#00E5FF;font-family:monospace'>"
                "<h2>CHRONO WiFi Setup</h2>"
                "<form action='/save' method='POST'>"
                "SSID:<br><input name='ssid'><br>Password:<br><input name='pass' type='password'><br><br>"
                "<input type='submit' value='Save'></form></body></html>";
  configServer.send(200, "text/html", html);
}

void handleConfigSave() {
  String s = configServer.arg("ssid");
  String p = configServer.arg("pass");
  wifiPrefs.begin("wifi", false);
  wifiPrefs.putString("ssid", s);
  wifiPrefs.putString("pass", p);
  wifiPrefs.end();
  configServer.send(200, "text/html", "<html><body>Saved. Rebooting...</body></html>");
  delay(2000);
  ESP.restart();
}

void startConfigPortal() {
  WiFi.mode(WIFI_AP);
  const char* apSSID = "CrowPanel-Config";
  const char* apPass = "12345678";
  WiFi.softAP(apSSID, apPass);
  configServer.on("/", handleConfigRoot);
  configServer.on("/save", HTTP_POST, handleConfigSave);
  configServer.begin();
  Serial.println("AP mode: connect to CrowPanel-Config and open 192.168.4.1");

  // On-display AP configuration screen (CHRONO colors)
  lv_obj_t* apScreen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(apScreen, lv_color_hex(0x020B1E), LV_PART_MAIN);
  lv_obj_clear_flag(apScreen, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title = lv_label_create(apScreen);
  lv_label_set_text(title, "WIFI SETUP");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
  lv_obj_set_style_text_color(title, lv_color_hex(0x00E5FF), LV_PART_MAIN);
  lv_obj_set_style_text_letter_space(title, 6, LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

  char buf[128];
  lv_obj_t* l1 = lv_label_create(apScreen);
  snprintf(buf, sizeof(buf), "SSID: %s", apSSID);
  lv_label_set_text(l1, buf);
  lv_obj_set_style_text_font(l1, &lv_font_montserrat_20, LV_PART_MAIN);
  lv_obj_set_style_text_color(l1, lv_color_hex(0x63A4FF), LV_PART_MAIN);
  lv_obj_align(l1, LV_ALIGN_CENTER, 0, -60);

  lv_obj_t* l2 = lv_label_create(apScreen);
  snprintf(buf, sizeof(buf), "Password: %s", apPass);
  lv_label_set_text(l2, buf);
  lv_obj_set_style_text_font(l2, &lv_font_montserrat_20, LV_PART_MAIN);
  lv_obj_set_style_text_color(l2, lv_color_hex(0x63A4FF), LV_PART_MAIN);
  lv_obj_align(l2, LV_ALIGN_CENTER, 0, -20);

  lv_obj_t* l3 = lv_label_create(apScreen);
  lv_label_set_text(l3, "Open http://192.168.4.1");
  lv_obj_set_style_text_font(l3, &lv_font_montserrat_20, LV_PART_MAIN);
  lv_obj_set_style_text_color(l3, lv_color_hex(0x00E676), LV_PART_MAIN);
  lv_obj_align(l3, LV_ALIGN_CENTER, 0, 40);

  lv_scr_load(apScreen);

  while (true) {
    configServer.handleClient();
    lv_timer_handler();
    delay(10);
  }
}

// ---- Zoned time helpers ----
bool getZonedTimes(struct tm &istTime, struct tm &ukTime) {
  time_t utcNow = time(nullptr);
  if (utcNow < VALID_TIME_EPOCH) return false;

  time_t istNow = utcNow + IST_OFFSET_SECONDS;
  if (gmtime_r(&istNow, &istTime) == nullptr) return false;
  return localtime_r(&utcNow, &ukTime) != nullptr; // UK via system TZ (BST-aware)
}

void onTimeSync(struct timeval *timeInfo) {
  (void)timeInfo;
  ntpSyncEvent = true;
}

const char* syncStateText() {
  switch (syncState) {
    case ConnectingWifi: return "CONNECTING";
    case SyncingTime: return "SYNCING";
    case Synced: return ntpSynced ? "NTP OK" : "SYNCING";
    case SyncFailed: return "NO SYNC";
    case OfflineCached: return "CACHED";
    case OfflineNoTime: return "NO SYNC";
  }
  return "NO SYNC";
}

void startNtpSync(uint32_t nowMs) {
  if (WiFi.status() != WL_CONNECTED) return;
  syncState = SyncingTime;
  ntpRequestStartedMs = nowMs;
  ntpSynced = false;
  ntpSyncEvent = false;
  sntp_set_time_sync_notification_cb(onTimeSync);
  configTzTime(UK_TZ_SPEC, NTP_SERVERS[0], NTP_SERVERS[1], NTP_SERVERS[2]);
  nextNtpAttemptMs = nowMs + NTP_REQUEST_TIMEOUT_MS;
  Serial.println("NTP sync requested with 3 fallback servers");
}

void serviceNetwork(uint32_t nowMs) {
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected != wifiWasConnected) {
    wifiWasConnected = connected;
    if (connected) {
      ipAddress = WiFi.localIP().toString();
      Serial.printf("WiFi connected: %s\n", ipAddress.c_str());
      wifiRetryDelayMs = WIFI_BACKOFF_MIN_MS;
      nextNtpAttemptMs = nowMs;
      startNtpSync(nowMs);
    } else {
      ipAddress = "NO WIFI";
      ntpSynced = false;
      syncState = ntpWasEverSynced ? OfflineCached : OfflineNoTime;
      nextWifiAttemptMs = nowMs + wifiRetryDelayMs;
      wifiRetryDelayMs = min(wifiRetryDelayMs * 2, WIFI_BACKOFF_MAX_MS);
      Serial.println("WiFi disconnected; reconnect scheduled");
    }
  }

  if (!connected && static_cast<int32_t>(nowMs - nextWifiAttemptMs) >= 0) {
    Serial.printf("WiFi reconnect attempt (next backoff %lu ms)\n", static_cast<unsigned long>(wifiRetryDelayMs));
    WiFi.begin(wifiSSID.c_str(), wifiPass.c_str());
    nextWifiAttemptMs = nowMs + wifiRetryDelayMs;
    wifiRetryDelayMs = min(wifiRetryDelayMs * 2, WIFI_BACKOFF_MAX_MS);
    if (syncState != OfflineCached && syncState != OfflineNoTime) syncState = ConnectingWifi;
  }

  if (connected && syncState != SyncingTime && static_cast<int32_t>(nowMs - nextNtpAttemptMs) >= 0) startNtpSync(nowMs);

  time_t current = time(nullptr);
  if (connected && ntpSyncEvent && current >= VALID_TIME_EPOCH && syncState == SyncingTime) {
    ntpSyncEvent = false;
    ntpSynced = true;
    hasCachedTime = true;
    ntpWasEverSynced = true;
    lastNtpSyncEpoch = current;
    lastNtpSyncMs = nowMs;
    syncState = Synced;
    nextNtpAttemptMs = nowMs + NTP_RESYNC_MS;
    Serial.printf("NTP time valid: %lld\n", static_cast<long long>(current));
  } else if (connected && syncState == SyncingTime && static_cast<int32_t>(nowMs - nextNtpAttemptMs) >= 0) {
    ntpSynced = false;
    syncState = ntpWasEverSynced ? OfflineCached : SyncFailed;
    nextNtpAttemptMs = nowMs + NTP_RETRY_MS;
    Serial.println("NTP sync timeout; will retry");
  }

  // Push network state to the dashboard (only on change)
  static bool lastWifi = false;
  static bool lastNtp = false;
  static SyncState lastState = ConnectingWifi;
  static String lastIp = "";
  if (connected != lastWifi || ntpSynced != lastNtp || syncState != lastState || ipAddress != lastIp) {
    lastWifi = connected;
    lastNtp = ntpSynced;
    lastState = syncState;
    lastIp = ipAddress;
    ui_update_network(connected, ntpSynced, syncStateText(), ipAddress.c_str());
  }
}

void setup()
{
  Serial.begin(115200);
  delay(1000); // Wait for serial to initialize
  Serial.println("=== CHRONO - CrowPanel 7\" HMI Display ===");
  Serial.println("Initializing system.");
  // Note: GPIO35-38 are reserved by OPI PSRAM on this board and must not be used

  Wire.begin(19, 20);
  // V3.0 touch timing control (PCA9557 IO expander)
  Out.reset();
  Out.setMode(IO_OUTPUT);
  Out.setState(IO0, IO_LOW);
  Out.setState(IO1, IO_LOW);
  delay(20);
  Out.setState(IO0, IO_HIGH);
  delay(100);
  Out.setMode(IO1, IO_INPUT);
  Serial.println("Initializing LCD...");
  lcd.begin();
  lcd.fillScreen(TFT_BLACK);
  delay(200);
  Serial.println("LCD initialized");

  Serial.println("Initializing LVGL...");
  lv_init();

  touch_init();

  screenWidth = lcd.width();
  screenHeight = lcd.height();

  lv_disp_draw_buf_init(&draw_buf, disp_draw_buf, NULL, screenWidth * screenHeight / 15);
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = screenWidth;
  disp_drv.ver_res = screenHeight;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);

  // Initialize backlight with PWM for brightness control
  #ifdef TFT_BL
  ledcSetup(1, 300, 8);
  ledcAttachPin(TFT_BL, 1);
  ledcWrite(1, brightness);
  #endif

  Serial.println("Initializing UI...");
  ui_init();
  ui_set_ampm_visible(!use24HourFormat ? true : false);
  ui_apply_accent(0x00E5FF); // cyan accent
  lv_timer_handler();
  Serial.println("UI initialized");

  // Load WiFi credentials
  loadWiFiCredentials();

  if (wifiSSID.length() == 0) {
    startConfigPortal(); // blocking; reboots after credentials are saved
  }

  // UK time display relies on the system TZ (Europe/London DST rules)
  setenv("TZ", UK_TZ_SPEC, 1);
  tzset();

  Serial.printf("Connecting to WiFi SSID: %s\n", wifiSSID.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  WiFi.begin(wifiSSID.c_str(), wifiPass.c_str());
  nextWifiAttemptMs = millis() + WIFI_BACKOFF_MIN_MS;
  nextNtpAttemptMs = millis();
  syncState = ConnectingWifi;

  lastInteractionTime = millis();
  Serial.println("CHRONO dashboard running");
}

// Function to set display brightness
void setBrightness(int level) {
  brightness = constrain(level, 0, 255);
  ledcWrite(1, brightness);
  Serial.print("Brightness set to: ");
  Serial.println(brightness);
}

extern "C" void applyTechTheme() {
  ui_apply_accent(0x00FFC8); // teal clock accent as default
}

// Interactive functions
extern "C" void increaseBrightness() {
  brightness = min(255, brightness + 25);
  setBrightness(brightness);
  lastInteractionTime = millis();
}

extern "C" void decreaseBrightness() {
  brightness = max(25, brightness - 25);
  setBrightness(brightness);
  lastInteractionTime = millis();
}

extern "C" void changeColorTheme() {
  // Cycle accent colors: cyan -> green -> amber -> magenta
  static const uint32_t accents[] = {0x00FFC8, 0x00E5FF, 0xFFC400, 0xFF6FD8};
  currentTheme = (currentTheme + 1) % 4;
  ui_apply_accent(accents[currentTheme]);
  lastInteractionTime = millis();
}

extern "C" void toggleTimeFormat() {
  use24HourFormat = !use24HourFormat;
  const char* fmt = use24HourFormat ? "24H" : "12H";
  lv_label_set_text(ui_FormatLabel, fmt);
  lv_label_set_text(ui_FormatIndicator, fmt);
  ui_set_ampm_visible(!use24HourFormat);
  lastInteractionTime = millis();
  Serial.print("Time format: ");
  Serial.println(fmt);
}

extern "C" void showTimeDetails() {
  char detailText[96];
  struct tm istTime, ukTime;
  if (getZonedTimes(istTime, ukTime)) {
    snprintf(detailText, sizeof(detailText), "IST UTC+5:30 | UK UTC+%d | WEEK %d",
             ukTime.tm_isdst > 0 ? 1 : 0, (istTime.tm_yday / 7) + 1);
  } else {
    snprintf(detailText, sizeof(detailText), "DUAL TIME: IST & UK | UPTIME %lu s", millis() / 1000);
  }
  lv_label_set_text(ui_StatusLabel, detailText);
  lastInteractionTime = millis();
}

// Per-second dashboard refresh
void updateTimeDisplay() {
  struct tm istTime, ukTime;
  if (getZonedTimes(istTime, ukTime)) {
    hasCachedTime = true;

    // Blink the colons every second so the clock visibly "ticks"
    static bool lastColonState = true;
    bool colonOn = istTime.tm_sec % 2 == 0;
    if (colonOn != lastColonState) {
      ui_set_colons_visible(colonOn);
      lastColonState = colonOn;
    }

    char istAmPm[3] = "";
    char ukAmPm[3] = "";
    int istH = istTime.tm_hour;
    int ukH = ukTime.tm_hour;
    if (!use24HourFormat) {
      bool istPM = istH >= 12;
      if (istH == 0) istH = 12; else if (istH > 12) istH -= 12;
      strcpy(istAmPm, istPM ? "PM" : "AM");
      bool ukPM = ukH >= 12;
      if (ukH == 0) ukH = 12; else if (ukH > 12) ukH -= 12;
      strcpy(ukAmPm, ukPM ? "PM" : "AM");
    }

    ui_update_time_columns(istH, istTime.tm_min, istAmPm, ukH, ukTime.tm_min, ukAmPm);

    static int lastDayOfYear = -1;
    if (lastDayOfYear != istTime.tm_yday) {
      char dateBuf[20];
      snprintf(dateBuf, sizeof(dateBuf), "%02d %s %04d",
               istTime.tm_mday, monthNames[istTime.tm_mon], istTime.tm_year + 1900);
      ui_update_date(dayNames[istTime.tm_wday], dateBuf);
      lastDayOfYear = istTime.tm_yday;
    }

    static int lastProgressFill = -1;
    int fillPct = (istTime.tm_hour * 3600 + istTime.tm_min * 60 + istTime.tm_sec) * 100 / 86400;
    if (lastProgressFill != fillPct) {
      ui_update_day_progress(fillPct);
      lastProgressFill = fillPct;
    }

    // Once per minute, log the displayed times for verification
    static int lastLoggedMin = -1;
    if (istTime.tm_min != lastLoggedMin) {
      lastLoggedMin = istTime.tm_min;
      Serial.printf("IST %02d:%02d:%02d | UK %02d:%02d:%02d\n",
                    istTime.tm_hour, istTime.tm_min, istTime.tm_sec,
                    ukTime.tm_hour, ukTime.tm_min, ukTime.tm_sec);
    }
  } else if (!hasCachedTime) {
    // No valid time yet: blank digits and idle bar
    ui_blank_time_columns();
    ui_update_day_progress(0);
  }
}

extern "C" void hideButtons() {
  lv_obj_add_flag(ui_BrightnessUpBtn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_BrightnessDownBtn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_ThemeBtn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_FormatBtn, LV_OBJ_FLAG_HIDDEN);
  buttonsVisible = false;
}

extern "C" void showButtons() {
  lv_obj_clear_flag(ui_BrightnessUpBtn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ui_BrightnessDownBtn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ui_ThemeBtn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ui_FormatBtn, LV_OBJ_FLAG_HIDDEN);
  buttonsVisible = true;
}

void loop()
{
  uint32_t nowMs = millis();

  serviceNetwork(nowMs);
  updateTimeDisplay();

  lv_timer_handler();
  delay(5);
}
