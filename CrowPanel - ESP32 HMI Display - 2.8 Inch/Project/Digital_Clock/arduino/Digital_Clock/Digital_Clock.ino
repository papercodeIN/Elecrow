#include <Arduino.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include <time.h>
#include <esp_sntp.h>
#include "secrets.h"

const char* NTP_SERVERS[] = {"time.google.com", "pool.ntp.org", "time.cloudflare.com"};
const long IST_OFFSET_SECONDS = 19800;
constexpr uint32_t WIFI_BACKOFF_MIN_MS = 2000;
constexpr uint32_t WIFI_BACKOFF_MAX_MS = 60000;
constexpr uint32_t NTP_REQUEST_TIMEOUT_MS = 20000;
constexpr uint32_t NTP_RETRY_MS = 30000;
constexpr uint32_t NTP_RESYNC_MS = 6UL * 60UL * 60UL * 1000UL;
constexpr time_t VALID_TIME_EPOCH = 1704067200;

TFT_eSPI tft = TFT_eSPI();

#define BG_COLOR      0x0861
#define ARMY_GREEN    0x07FF
#define ARMY_AMBER    0xFDE0
#define DIM_GREEN     0x3D7A
#define GRID_COLOR    0x19A5
#define HEADER_BG     0x10A3
#define PANEL_BG      0x10A3
#define WHITE_COLOR   0xFFFF
#define GOOD_COLOR    0x07E0
#define BAD_COLOR     0xF800

#define USE_12H_FORMAT  1
#define CLOCK_SHOW_SECONDS 0

const uint8_t SEGMENT_MAP[10] = {
  0x3F, 0x06, 0x5B, 0x4F, 0x66,
  0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

const char* dayNames[] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
const char* monthNames[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

enum class SyncState : uint8_t {
  ConnectingWifi,
  SyncingTime,
  Synced,
  SyncFailed,
  OfflineCached,
  OfflineNoTime
};

int lastSecond = -1;
int lastProgressFill = -1;
bool ntpSynced = false;
bool use12h = true;
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
SyncState syncState = SyncState::ConnectingWifi;
String ipAddress = "NO WIFI";

void onTimeSync(struct timeval *timeInfo) {
  (void)timeInfo;
  ntpSyncEvent = true;
}

void drawHSegment(TFT_eSprite &spr, int x, int y, int len, int thick, uint32_t color) {
  for (int t = 0; t < thick; t++) {
    int shrink = (t < thick / 2) ? t : thick - 1 - t;
    spr.drawFastHLine(x + shrink + 1, y + t, len - 2 * shrink - 2, color);
  }
}

void drawVSegment(TFT_eSprite &spr, int x, int y, int len, int thick, uint32_t color) {
  for (int t = 0; t < thick; t++) {
    int shrink = (t < thick / 2) ? t : thick - 1 - t;
    spr.drawFastVLine(x + t, y + shrink + 1, len - 2 * shrink - 2, color);
  }
}

void drawDigit(TFT_eSprite &spr, int x, int y, int digit, int w, int h, int thick, uint32_t color) {
  uint8_t seg = SEGMENT_MAP[digit];
  int halfH = h / 2;

  if (seg & 0x01) drawHSegment(spr, x, y, w, thick, color);
  if (seg & 0x02) drawVSegment(spr, x + w - thick, y, halfH, thick, color);
  if (seg & 0x04) drawVSegment(spr, x + w - thick, y + halfH, halfH, thick, color);
  if (seg & 0x08) drawHSegment(spr, x, y + h - thick, w, thick, color);
  if (seg & 0x10) drawVSegment(spr, x, y + halfH, halfH, thick, color);
  if (seg & 0x20) drawVSegment(spr, x, y, halfH, thick, color);
  if (seg & 0x40) drawHSegment(spr, x, y + halfH - thick / 2, w, thick, color);
}

void drawColon(TFT_eSprite &spr, int x, int y, int h, int dotSize, uint32_t color) {
  int quarter = h / 4;
  spr.fillRect(x, y + quarter - dotSize / 2, dotSize, dotSize, color);
  spr.fillRect(x, y + 3 * quarter - dotSize / 2, dotSize, dotSize, color);
}

void drawTftDigit(int x, int y, int digit, int w, int h, int thick, uint32_t color) {
  uint8_t seg = SEGMENT_MAP[digit];
  int halfH = h / 2;

  if (seg & 0x01) tft.fillRect(x + thick / 2, y, w - thick, thick, color);
  if (seg & 0x02) tft.fillRect(x + w - thick, y + thick / 2, thick, halfH - thick, color);
  if (seg & 0x04) tft.fillRect(x + w - thick, y + halfH + thick / 2, thick, halfH - thick, color);
  if (seg & 0x08) tft.fillRect(x + thick / 2, y + h - thick, w - thick, thick, color);
  if (seg & 0x10) tft.fillRect(x, y + halfH + thick / 2, thick, halfH - thick, color);
  if (seg & 0x20) tft.fillRect(x, y + thick / 2, thick, halfH - thick, color);
  if (seg & 0x40) tft.fillRect(x + thick / 2, y + halfH - thick / 2, w - thick, thick, color);
}

void drawTftColon(int x, int y, int h, int dotSize, uint32_t color) {
  int quarter = h / 4;
  tft.fillRect(x, y + quarter - dotSize / 2, dotSize, dotSize, color);
  tft.fillRect(x, y + 3 * quarter - dotSize / 2, dotSize, dotSize, color);
}

const char* syncStateText();
void drawStatusHeader();

void drawHeader() {
  tft.fillRect(0, 0, 320, 30, HEADER_BG);
  tft.fillRect(0, 29, 320, 2, ARMY_GREEN);
  tft.setTextColor(ARMY_GREEN, HEADER_BG);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("CHRONO", 10, 7);
  tft.setTextColor(ARMY_AMBER, HEADER_BG);
  tft.setFreeFont(&FreeMono9pt7b);
  tft.setTextDatum(TR_DATUM);
  tft.drawString(use12h ? "12H" : "24H", 310, 7);
  drawStatusHeader();
}

void drawStatusHeader() {
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  tft.fillRect(103, 3, 100, 24, HEADER_BG);
  tft.fillCircle(113, 15, 4, wifiConnected ? GOOD_COLOR : ARMY_AMBER);
  tft.setFreeFont(&FreeMono9pt7b);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(wifiConnected ? GOOD_COLOR : ARMY_AMBER, HEADER_BG);
  tft.drawString(wifiConnected ? "WIFI" : "NO WIFI", 122, 15);
  tft.fillRect(207, 3, 107, 24, HEADER_BG);
  tft.fillCircle(216, 15, 4, ntpSynced ? GOOD_COLOR : ARMY_AMBER);
  tft.setTextColor(ntpSynced ? GOOD_COLOR : ARMY_AMBER, HEADER_BG);
  char state[16];
  snprintf(state, sizeof(state), "%s", ntpSynced ? "NTP OK" :
           syncState == SyncState::SyncingTime ? "SYNCING" :
           syncState == SyncState::OfflineCached ? "CACHED" : "NO SYNC");
  tft.drawString(state, 225, 15);
}

void drawFooter() {
  tft.fillRect(0, 214, 320, 26, HEADER_BG);
  tft.fillRect(0, 214, 320, 2, ARMY_GREEN);
  tft.setTextColor(DIM_GREEN, HEADER_BG);
  tft.setFreeFont(&FreeMono9pt7b);
  tft.setTextDatum(ML_DATUM);
  tft.drawString("NETWORK", 10, 228);
  tft.setTextColor(wifiWasConnected ? GOOD_COLOR : ARMY_AMBER, HEADER_BG);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(ipAddress, 310, 228);
}

void drawBorder() {
  tft.fillRoundRect(8, 34, 304, 174, 8, PANEL_BG);
  tft.drawRoundRect(8, 34, 304, 174, 8, DIM_GREEN);
  tft.drawFastVLine(160, 52, 77, GRID_COLOR);
}

bool getZonedTimes(struct tm &istTime, struct tm &ukTime) {
  time_t utcNow = time(nullptr);
  if (utcNow < VALID_TIME_EPOCH) return false;

  time_t istNow = utcNow + IST_OFFSET_SECONDS;
  if (gmtime_r(&istNow, &istTime) == nullptr) return false;
  setenv("TZ", "GMT0BST,M3.5.0/1,M10.5.0/2", 1);
  tzset();
  return localtime_r(&utcNow, &ukTime) != nullptr;
}

const char* syncStateText() {
  switch (syncState) {
    case SyncState::ConnectingWifi: return "CONNECTING WIFI";
    case SyncState::SyncingTime: return "SYNCING TIME";
    case SyncState::Synced: return "SYNCED";
    case SyncState::SyncFailed: return "SYNC FAILED";
    case SyncState::OfflineCached: return "OFFLINE / CACHED";
    case SyncState::OfflineNoTime: return "OFFLINE / NO TIME";
  }
  return "TIME ERROR";
}

void updateStatus() {
  drawStatusHeader();
  drawFooter();
}

void drawLastSync() {
  drawStatusHeader();
}

void startNtpSync(uint32_t nowMs) {
  if (WiFi.status() != WL_CONNECTED) return;
  syncState = SyncState::SyncingTime;
  ntpRequestStartedMs = nowMs;
  ntpSynced = false;
  ntpSyncEvent = false;
  sntp_set_time_sync_notification_cb(onTimeSync);
  configTzTime("UTC0", NTP_SERVERS[0], NTP_SERVERS[1], NTP_SERVERS[2]);
  nextNtpAttemptMs = nowMs + NTP_REQUEST_TIMEOUT_MS;
  Serial.println("NTP sync requested with 3 fallback servers");
  drawStatusHeader();
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
      syncState = ntpWasEverSynced ? SyncState::OfflineCached : SyncState::OfflineNoTime;
      nextWifiAttemptMs = nowMs + wifiRetryDelayMs;
      wifiRetryDelayMs = min(wifiRetryDelayMs * 2, WIFI_BACKOFF_MAX_MS);
      Serial.println("WiFi disconnected; reconnect scheduled");
    }
    drawHeader();
    drawFooter();
    updateStatus();
  }

  if (!connected && static_cast<int32_t>(nowMs - nextWifiAttemptMs) >= 0) {
    Serial.printf("WiFi reconnect attempt (next backoff %lu ms)\n", static_cast<unsigned long>(wifiRetryDelayMs));
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    nextWifiAttemptMs = nowMs + wifiRetryDelayMs;
    wifiRetryDelayMs = min(wifiRetryDelayMs * 2, WIFI_BACKOFF_MAX_MS);
    if (syncState != SyncState::OfflineCached && syncState != SyncState::OfflineNoTime) syncState = SyncState::ConnectingWifi;
    updateStatus();
  }

  if (connected && syncState != SyncState::SyncingTime && static_cast<int32_t>(nowMs - nextNtpAttemptMs) >= 0) startNtpSync(nowMs);

  time_t current = time(nullptr);
  if (connected && ntpSyncEvent && current >= VALID_TIME_EPOCH && syncState == SyncState::SyncingTime) {
    ntpSyncEvent = false;
    ntpSynced = true;
    hasCachedTime = true;
    ntpWasEverSynced = true;
    lastNtpSyncEpoch = current;
    lastNtpSyncMs = nowMs;
    syncState = SyncState::Synced;
    nextNtpAttemptMs = nowMs + NTP_RESYNC_MS;
    Serial.printf("NTP time valid: %lld\n", static_cast<long long>(current));
    drawHeader();
    updateStatus();
    drawLastSync();
  } else if (connected && syncState == SyncState::SyncingTime && static_cast<int32_t>(nowMs - nextNtpAttemptMs) >= 0) {
    ntpSynced = false;
    syncState = ntpWasEverSynced ? SyncState::OfflineCached : SyncState::SyncFailed;
    nextNtpAttemptMs = nowMs + NTP_RETRY_MS;
    Serial.println("NTP sync timeout; will retry");
    drawHeader();
    updateStatus();
  }
}


void formatClockTime(struct tm &t, char *timeBuf, char *periodBuf) {
  int h = t.tm_hour;
  periodBuf[0] = '\0';

  if (use12h) {
    bool isPM = h >= 12;
    if (h == 0) h = 12;
    else if (h > 12) h -= 12;
    sprintf(timeBuf, "%d:%02d", h, t.tm_min);
    strcpy(periodBuf, isPM ? "PM" : "AM");
  } else {
    sprintf(timeBuf, "%02d:%02d", h, t.tm_min);
  }
}

void drawTimeColumn(int centerX, const char *label, struct tm &t, uint32_t color) {
  char periodBuf[3];
  int h = t.tm_hour;
  periodBuf[0] = '\0';

  if (use12h) {
    bool isPM = h >= 12;
    if (h == 0) h = 12;
    else if (h > 12) h -= 12;
    strcpy(periodBuf, isPM ? "PM" : "AM");
  }

  int digitW = 18;
  int digitH = 34;
  int thick = 4;
  int gap = 3;
  int colonW = 8;
  int totalW = digitW * 4 + gap * 3 + colonW;
  int x = centerX - totalW / 2;
  int y = 72;

  tft.setTextColor(ARMY_AMBER, PANEL_BG);
  tft.setFreeFont(&FreeMono9pt7b);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(label, centerX, 38);

  drawTftDigit(x, y, h / 10, digitW, digitH, thick, color);
  drawTftDigit(x + digitW + gap, y, h % 10, digitW, digitH, thick, color);
  drawTftColon(x + digitW * 2 + gap * 2, y, digitH, 5, color);
  drawTftDigit(x + digitW * 2 + gap * 2 + colonW, y, t.tm_min / 10, digitW, digitH, thick, color);
  drawTftDigit(x + digitW * 3 + gap * 3 + colonW, y, t.tm_min % 10, digitW, digitH, thick, color);
#if CLOCK_SHOW_SECONDS
  drawTftColon(x + digitW * 4 + gap * 3 + colonW, y, digitH, 4, color);
  drawTftDigit(x + digitW * 4 + gap * 3 + colonW + 6, y, t.tm_sec / 10, digitW, digitH, thick, color);
  drawTftDigit(x + digitW * 5 + gap * 3 + colonW + 6, y, t.tm_sec % 10, digitW, digitH, thick, color);
#endif

  if (use12h) {
    tft.setTextColor(ARMY_AMBER, BG_COLOR);
    tft.setFreeFont(&FreeMono9pt7b);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(periodBuf, centerX, 112);
  }
}

void renderTime(struct tm &istTime, struct tm &ukTime) {
  static int prevIstHour = -1;
  static int prevIstMinute = -1;
  static int prevUkHour = -1;
  static int prevUkMinute = -1;
  if (prevIstHour == istTime.tm_hour && prevIstMinute == istTime.tm_min &&
      prevUkHour == ukTime.tm_hour && prevUkMinute == ukTime.tm_min) return;
  tft.fillRect(12, 53, 296, 77, PANEL_BG);
  tft.drawFastVLine(160, 52, 77, GRID_COLOR);
  tft.drawFastHLine(20, 52, 280, GRID_COLOR);
  drawTimeColumn(85, "IST", istTime, ARMY_GREEN);
  drawTimeColumn(235, "UK", ukTime, ARMY_GREEN);
  prevIstHour = istTime.tm_hour;
  prevIstMinute = istTime.tm_min;
  prevUkHour = ukTime.tm_hour;
  prevUkMinute = ukTime.tm_min;
}

void renderDate(struct tm &t) {
  tft.fillRect(15, 132, 290, 38, PANEL_BG);

  tft.setTextColor(DIM_GREEN, PANEL_BG);
  tft.setFreeFont(&FreeMono9pt7b);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(dayNames[t.tm_wday], 160, 138);

  tft.setTextColor(WHITE_COLOR, PANEL_BG);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextDatum(TC_DATUM);

  char dateBuf[32];
  sprintf(dateBuf, "%02d %s %04d", t.tm_mday, monthNames[t.tm_mon], t.tm_year + 1900);
  tft.drawString(dateBuf, 160, 158);
}

void renderProgressBar(struct tm &t) {
  const int barY = 180;
  const int barH = 7;
  const int barW = 260;
  const int barX = 30;
  const uint32_t seconds = static_cast<uint32_t>(t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec);
  const int fillW = static_cast<int>((static_cast<uint64_t>(barW) * seconds) / 86400U);
  if (fillW == lastProgressFill) return;
  lastProgressFill = fillW;

  tft.drawRect(barX - 1, barY - 1, barW + 2, barH + 2, DIM_GREEN);
  tft.fillRect(barX, barY, fillW, barH, ARMY_GREEN);
  tft.fillRect(barX + fillW, barY, barW - fillW, barH, GRID_COLOR);

  tft.setTextColor(ARMY_AMBER, PANEL_BG);
  tft.setFreeFont(&FreeMono9pt7b);
  tft.setTextDatum(TC_DATUM);
  char pctBuf[8];
  snprintf(pctBuf, sizeof(pctBuf), "%d%%", static_cast<int>((seconds * 100ULL) / 86400ULL));
  tft.fillRect(130, 190, 60, 15, PANEL_BG);
  tft.drawString(pctBuf, 160, 193);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Booting WiFi clock");
  use12h = USE_12H_FORMAT;

  pinMode(27, OUTPUT);
  ledcSetup(0, 5000, 8);
  ledcAttachPin(27, 0);
  ledcWrite(0, 200);

  tft.init();
  tft.invertDisplay(false);
  tft.setRotation(1);
  tft.fillScreen(BG_COLOR);
  drawHeader();
  drawBorder();
  drawFooter();
  updateStatus();
  drawLastSync();

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  nextWifiAttemptMs = millis() + WIFI_BACKOFF_MIN_MS;
  nextNtpAttemptMs = millis();
  syncState = SyncState::ConnectingWifi;
  Serial.printf("Connecting to WiFi SSID: %s\n", WIFI_SSID);
}

void loop() {
  const uint32_t nowMs = millis();
  serviceNetwork(nowMs);

  struct tm istTime;
  struct tm ukTime;
  if (getZonedTimes(istTime, ukTime)) {
    hasCachedTime = true;
    if (lastSecond != istTime.tm_sec) {
      renderProgressBar(istTime);
      lastSecond = istTime.tm_sec;
    }
    renderTime(istTime, ukTime);
    static int lastDateDay = -1;
    if (lastDateDay != istTime.tm_yday) {
      renderDate(istTime);
      lastDateDay = istTime.tm_yday;
    }
  } else if (syncState != SyncState::ConnectingWifi && syncState != SyncState::SyncingTime && syncState != SyncState::SyncFailed) {
    syncState = SyncState::OfflineNoTime;
    updateStatus();
  }

  static uint32_t lastUptimeUpdateMs = 0;
  if (nowMs - lastUptimeUpdateMs >= 60000) {
    lastUptimeUpdateMs = nowMs;
    drawStatusHeader();
  }
  delay(20);
}
