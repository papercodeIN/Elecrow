#include <PCA9557.h>
#include <lvgl.h>
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "ui.h"
#include "webui.h"
// secrets.h is git-ignored (may hold real credentials); the device falls
// back to NVS-stored credentials or AP configuration mode without it
#if __has_include("secrets.h")
#include "secrets.h"
#endif

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
      
      cfg.pin_d0  = GPIO_NUM_15; cfg.pin_d1  = GPIO_NUM_7;
      cfg.pin_d2  = GPIO_NUM_6;  cfg.pin_d3  = GPIO_NUM_5;
      cfg.pin_d4  = GPIO_NUM_4;  cfg.pin_d5  = GPIO_NUM_9;
      cfg.pin_d6  = GPIO_NUM_46; cfg.pin_d7  = GPIO_NUM_3;
      cfg.pin_d8  = GPIO_NUM_8;  cfg.pin_d9  = GPIO_NUM_16;
      cfg.pin_d10 = GPIO_NUM_1;  cfg.pin_d11 = GPIO_NUM_14;
      cfg.pin_d12 = GPIO_NUM_21; cfg.pin_d13 = GPIO_NUM_47;
      cfg.pin_d14 = GPIO_NUM_48; cfg.pin_d15 = GPIO_NUM_45;

      cfg.pin_henable = GPIO_NUM_41;
      cfg.pin_vsync   = GPIO_NUM_40;
      cfg.pin_hsync   = GPIO_NUM_39;
      cfg.pin_pclk    = GPIO_NUM_0;
      cfg.freq_write  = 14000000;

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
int brightness = 255;

const char* ap_ssid = "CrowPanel_Display";
const char* ap_password = "12345678";
WebServer server(80);
DNSServer dnsServer;
Preferences preferences;
bool isAPMode = false;
String wifiStatus = "Connecting...";
String savedSSID = "";
String savedPassword = "";

String displayText[5] = {"LINE 1", "LINE 2", "LINE 3", "LINE 4", "LINE 5"};
String textColor[5] = {"#00E0FF", "#FF3FBF", "#39FF14", "#FFFFFF", "#FF9F1A"};
int textSize[5] = {48, 48, 48, 48, 48};
String backgroundColor = "#0D1117";
unsigned long lastUpdate = 0;

#define TFT_BL 2
SPIClass& spi = SPI;
#include "touch.h"

static uint32_t screenWidth;
static uint32_t screenHeight;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t disp_draw_buf[800 * 480 / 15];
static lv_disp_drv_t disp_drv;

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  lcd.pushImageDMA(area->x1, area->y1, w, h, (lgfx::rgb565_t*)&color_p->full);
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

void startAPMode() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);

  IPAddress apIP = WiFi.softAPIP();
  Serial.print("AP Mode started. IP address: ");
  Serial.println(apIP);

  wifiStatus = "AP MODE: " + apIP.toString();
  isAPMode = true;

  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", apIP);
}

void loadWiFiCredentials() {
  preferences.begin("crowpanel_v3", true); // read-only
  savedSSID = preferences.getString("ssid", "");
  savedPassword = preferences.getString("pass", "");
  preferences.end();
  
  // If no credentials saved in NVS, fall back to secrets.h
  if (savedSSID.length() == 0) {
#if defined(WIFI_SSID)
    savedSSID = WIFI_SSID;
#endif
#if defined(WIFI_PASS)
    savedPassword = WIFI_PASS;
#endif
  }
}

void saveWiFiCredentials(const String &ssid, const String &pass) {
  preferences.begin("crowpanel_v3", false);
  preferences.putString("ssid", ssid);
  preferences.putString("pass", pass);
  preferences.end();
  savedSSID = ssid;
  savedPassword = pass;
}

void clearWiFiCredentials() {
  preferences.begin("crowpanel_v3", false);
  preferences.clear();
  preferences.end();
  savedSSID = "";
  savedPassword = "";
}

String deviceURL() {
  IPAddress ip = isAPMode ? WiFi.softAPIP() : WiFi.localIP();
  return "http://" + ip.toString();
}

void setupWiFi() {
  Serial.println("Setting up WiFi...");
  loadWiFiCredentials();
  
  if (savedSSID.length() == 0) {
    Serial.println("No WiFi credentials found. Starting AP mode for configuration...");
    startAPMode();
    return;
  }
  
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    wifiStatus = WiFi.localIP().toString();
    isAPMode = false;
  } else {
    Serial.println("\nFailed to connect to WiFi. Starting AP mode...");
    startAPMode();
  }
}

String escapeHtml(const String &in) {
  String out;
  out.reserve(in.length() + 16);
  for (unsigned int i = 0; i < in.length(); i++) {
    char c = in.charAt(i);
    switch (c) {
      case '&':  out += "&amp;";  break;
      case '<':  out += "&lt;";   break;
      case '>':  out += "&gt;";   break;
      case '"':  out += "&quot;"; break;
      case '\'': out += "&#39;";  break;
      default:   out += c;        break;
    }
  }
  return out;
}

String buildLinesHtml() {
  String lines = "";
  for(int i = 0; i < 5; i++) {
    lines += "<div class=\"row\">";
    lines += "<div class=\"ch\">LINE " + String(i + 1) + "</div>";
    lines += "<div><label>TEXT</label><input type=\"text\" id=\"text" + String(i) + "\" name=\"text" + String(i) + "\" value=\"" + escapeHtml(displayText[i]) + "\" maxlength=\"50\" placeholder=\"enter text\"></div>";
    lines += "<div><label>COLOR</label><input type=\"color\" id=\"color" + String(i) + "\" name=\"color" + String(i) + "\" value=\"" + textColor[i] + "\"></div>";
    lines += "<div><label>SIZE</label><select id=\"size" + String(i) + "\" name=\"size" + String(i) + "\">";
    lines += "<option value=\"18\"" + String(textSize[i] == 18 ? " selected" : "") + ">SMALL</option>";
    lines += "<option value=\"24\"" + String(textSize[i] == 24 ? " selected" : "") + ">MEDIUM</option>";
    lines += "<option value=\"36\"" + String(textSize[i] == 36 ? " selected" : "") + ">LARGE</option>";
    lines += "<option value=\"48\"" + String(textSize[i] == 48 ? " selected" : "") + ">XLARGE</option>";
    lines += "</select></div>";
    lines += "</div>";
  }
  return lines;
}

void handleRoot() {
  String html = FPSTR(WEBUI_HTML);

  String mode = isAPMode ? "ACCESS_POINT" : "STATION_CLIENT";

  // Reset panel is only offered in station mode; in AP mode there is nothing to reset
  String resetSection = "";
  if (!isAPMode) {
    resetSection = FPSTR(WEBUI_RESET_SECTION);
  }

  html.replace("%WIFI_STATUS%", wifiStatus);
  html.replace("%MODE%", mode);
  html.replace("%AP_IP%", deviceURL());
  html.replace("%RESET_SECTION%", resetSection);
  html.replace("%BGCOLOR%", backgroundColor);
  html.replace("%LINES%", buildLinesHtml());

  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.send(200, "text/html", html);
}

void handleUpdate() {
  if (server.hasArg("bgcolor")) {
    backgroundColor = server.arg("bgcolor");
  }
  
  for(int i = 0; i < 5; i++) {
    String textArg = "text" + String(i);
    String colorArg = "color" + String(i);
    String sizeArg = "size" + String(i);
    
    if (server.hasArg(textArg)) {
      displayText[i] = server.arg(textArg);
    }
    if (server.hasArg(colorArg)) {
      textColor[i] = server.arg(colorArg);
    }
    if (server.hasArg(sizeArg)) {
      textSize[i] = server.arg(sizeArg).toInt();
    }
  }
  
  lastUpdate = 0;
  server.send(200, "text/plain", "OK");
}

void handleNotFound() {
  // Absolute redirect: captive-portal probes from Android/iOS/Windows all land on the UI
  server.sendHeader("Location", deviceURL() + "/", true);
  server.send(302, "text/plain", "");
}

void handleWifiConfig() {
  if (server.hasArg("ssid")) {
    String ssid = server.arg("ssid");
    String pass = server.hasArg("pass") ? server.arg("pass") : "";
    saveWiFiCredentials(ssid, pass);
    String html = "<html><head><meta charset='UTF-8'><meta http-equiv='refresh' content='4;url=/'><title>WiFi Saved</title><style>body{background:#000;color:#39ff14;font-family:JetBrains Mono,monospace;display:flex;align-items:center;justify-content:center;height:100vh;text-align:center;line-height:1.8;} .box{border:1px solid #00e0ff;padding:40px;background:radial-gradient(ellipse at top,rgba(0,224,255,0.08),#000);}</style></head><body><div class='box'>[ CREDENTIALS SAVED ]<br>REBOOTING TO CONNECT TO:<br><b>" + ssid + "</b><br><br>DEVICE WILL RESTART IN 3 SECONDS...</div></body></html>";
    server.send(200, "text/html", html);
    delay(1500);
    ESP.restart();
    return;
  }
  server.send(400, "text/plain", "Missing SSID");
}

void handleWifiReset() {
  clearWiFiCredentials();
  String html = "<html><head><meta charset='UTF-8'><meta http-equiv='refresh' content='4;url=/'><title>WiFi Reset</title><style>body{background:#000;color:#ff9f1a;font-family:JetBrains Mono,monospace;display:flex;align-items:center;justify-content:center;height:100vh;text-align:center;line-height:1.8;} .box{border:1px solid #ff9f1a;padding:40px;background:radial-gradient(ellipse at top,rgba(255,159,26,0.08),#000);}</style></head><body><div class='box'>[ CREDENTIALS CLEARED ]<br>DEVICE WILL RESTART IN AP MODE:<br><b>CrowPanel_Display</b> / 12345678<br><br>DEVICE WILL RESTART IN 3 SECONDS...</div></body></html>";
  server.send(200, "text/html", html);
  Serial.println("WiFi credentials cleared. Rebooting into AP mode...");
  delay(1500);
  ESP.restart();
}

void updateDisplayText() {
  static String lastDisplayText[5];
  static String lastTextColor[5];
  static int lastTextSize[5];
  static String lastWifiStatus = "";
  static String lastBackgroundColor = "";
  
  if (backgroundColor != lastBackgroundColor) {
    uint32_t bgColorValue = strtol(backgroundColor.c_str() + 1, NULL, 16);
    lv_obj_set_style_bg_color(ui_Screen1, lv_color_hex(bgColorValue), LV_PART_MAIN | LV_STATE_DEFAULT);
    lastBackgroundColor = backgroundColor;
  }
  
  for(int i = 0; i < 5; i++) {
    if (displayText[i] != lastDisplayText[i] || textColor[i] != lastTextColor[i] || textSize[i] != lastTextSize[i]) {
      lv_label_set_text(ui_TextLabel[i], displayText[i].c_str());
      
      uint32_t colorValue = strtol(textColor[i].c_str() + 1, NULL, 16);
      lv_obj_set_style_text_color(ui_TextLabel[i], lv_color_hex(colorValue), LV_PART_MAIN | LV_STATE_DEFAULT);
      
      if (textSize[i] <= 18) {
        lv_obj_set_style_text_font(ui_TextLabel[i], &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
      } else if (textSize[i] <= 24) {
        lv_obj_set_style_text_font(ui_TextLabel[i], &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
      } else if (textSize[i] <= 36) {
        lv_obj_set_style_text_font(ui_TextLabel[i], &lv_font_montserrat_36, LV_PART_MAIN | LV_STATE_DEFAULT);
      } else {
        lv_obj_set_style_text_font(ui_TextLabel[i], &lv_font_montserrat_48, LV_PART_MAIN | LV_STATE_DEFAULT);
      }
      
      lastDisplayText[i] = displayText[i];
      lastTextColor[i] = textColor[i];
      lastTextSize[i] = textSize[i];
    }
  }
  
  if (wifiStatus != lastWifiStatus) {
    lv_label_set_text(ui_StatusLabel, wifiStatus.c_str());
    lastWifiStatus = wifiStatus;
  }
}

void setup(){
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== CROWPANEL WIRELESS TEXT DISPLAY ===");

  pinMode(38, OUTPUT);
  digitalWrite(38, LOW);

  Wire.begin(19, 20);

  // V3.0 panel power/reset timing (PCA9557 IO expander):
  // IO0 = LCD reset, IO1 = GT911 touch reset
  Out.reset();
  Out.setMode(IO_OUTPUT);
  Out.setState(IO0, IO_LOW);
  Out.setState(IO1, IO_LOW);
  delay(20);
  Out.setState(IO0, IO_HIGH);
  delay(100);
  Out.setMode(IO1, IO_INPUT);

  lcd.begin();
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextSize(2);
  delay(200);
  
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

#ifdef TFT_BL
  ledcSetup(1, 300, 8);
  ledcAttachPin(TFT_BL, 1);
  ledcWrite(1, brightness);
#endif

  ui_init();
  lv_timer_handler();
  
  setupWiFi();

  // Show the AP configuration screen when WiFi is not configured/unreachable,
  // otherwise the main HUD screen keeps the IP visible in its status bar
  if (isAPMode) {
    ui_APScreen_screen_init();
    lv_disp_load_scr(ui_APScreen);
  } else {
    lv_label_set_text(ui_StatusLabel, wifiStatus.c_str());
  }
  lv_timer_handler();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/update", HTTP_POST, handleUpdate);
  server.on("/api/wifi", HTTP_POST, handleWifiConfig);
  server.on("/api/wifi/reset", HTTP_POST, handleWifiReset);
  // Common OS connectivity probes: redirecting them is what triggers the
  // captive-portal popup on phones; registering them explicitly keeps the
  // WebServer from logging "request handler not found" for each one.
  server.on("/favicon.ico", HTTP_GET, []() { server.send(204); });
  server.on("/generate_204", HTTP_GET, handleNotFound);          // Android
  server.on("/gen_204", HTTP_GET, handleNotFound);               // Android
  server.on("/hotspot-detect.html", HTTP_GET, handleNotFound);   // Apple
  server.on("/library/test/success.html", HTTP_GET, handleNotFound); // Apple
  server.on("/connecttest.txt", HTTP_GET, handleNotFound);       // Windows
  server.on("/ncsi.txt", HTTP_GET, handleNotFound);              // Windows
  server.onNotFound(handleNotFound);
  
  server.begin();
  Serial.println("HUD Interface Active");
}

void loop()
{
  server.handleClient();
  
  if (isAPMode) {
    dnsServer.processNextRequest();
  }
  
  if(millis() - lastUpdate > 1000) {
    updateDisplayText();
    lastUpdate = millis();
  }

  // Keep the on-screen IP status current (and visible if the link drops).
  // Only assign when the value actually differs so LVGL is never invalidated needlessly.
  static unsigned long lastNetCheck = 0;
  if (!isAPMode && millis() - lastNetCheck > 5000) {
    lastNetCheck = millis();
    String netNow = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "RECONNECTING...";
    if (wifiStatus != netNow) {
      wifiStatus = netNow;
    }
  }

  lv_timer_handler();
  delay(10);
}
