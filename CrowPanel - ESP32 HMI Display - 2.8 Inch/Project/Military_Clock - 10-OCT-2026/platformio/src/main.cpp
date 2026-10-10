#include <Arduino.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include <time.h>
#include "secrets.h"

const char* NTP_SERVER = "pool.ntp.org";
const long  GMT_OFFSET = 19800;
const int   DST_OFFSET = 0;

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite timeSprite = TFT_eSprite(&tft);

#define BG_COLOR      0x0000
#define ARMY_GREEN    0x4E45
#define ARMY_AMBER    0xFD20
#define DIM_GREEN     0x2D44
#define GRID_COLOR    0x1221
#define HEADER_BG     0x1221

#define USE_12H_FORMAT  1

const uint8_t SEGMENT_MAP[10] = {
  0x3F, 0x06, 0x5B, 0x4F, 0x66,
  0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

const char* dayNames[] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
const char* monthNames[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

int lastMin = -1;
bool ntpSynced = false;
bool use12h = true;
String ipAddress = "";

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

void drawHeader() {
  tft.fillRect(0, 0, 320, 28, HEADER_BG);
  tft.drawFastHLine(0, 28, 320, ARMY_GREEN);
  tft.drawFastHLine(0, 29, 320, ARMY_GREEN);

  tft.setFreeFont(&FreeMono9pt7b);
  tft.setTextDatum(ML_DATUM);
  if (ntpSynced) {
    tft.setTextColor(ARMY_GREEN, HEADER_BG);
    tft.drawString("NTP:OK", 8, 14);
  } else {
    tft.setTextColor(ARMY_AMBER, HEADER_BG);
    tft.drawString("NTP:WAIT", 8, 14);
  }

  tft.setTextColor(ARMY_GREEN, HEADER_BG);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("[ TIME ]", 160, 14);

  tft.setTextColor(ARMY_AMBER, HEADER_BG);
  tft.setFreeFont(&FreeMono9pt7b);
  tft.setTextDatum(TR_DATUM);
  tft.drawString(use12h ? "12H" : "24H", 312, 14);
}

void drawFooter() {
  tft.fillRect(0, 140, 320, 25, HEADER_BG);
  tft.drawFastHLine(0, 140, 320, ARMY_GREEN);
  tft.drawFastHLine(0, 141, 320, ARMY_GREEN);

  tft.setTextColor(DIM_GREEN, HEADER_BG);
  tft.setFreeFont(&FreeMono9pt7b);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(ipAddress, 160, 153);
}

void drawBorder() {
  tft.drawRect(0, 30, 320, 100, DIM_GREEN);
  tft.drawRect(1, 31, 318, 98, DIM_GREEN);
}

void renderTime(struct tm &t) {
  timeSprite.fillSprite(BG_COLOR);

  int h = t.tm_hour;
  int m = t.tm_min;

  bool isPM = false;
  if (use12h) {
    isPM = h >= 12;
    if (h == 0) h = 12;
    else if (h > 12) h -= 12;
  }

  int digitW = 26;
  int digitH = 40;
  int thick  = 4;
  int gap    = 2;

  int startX = (320 - (digitW * 4 + gap * 3 + 16)) / 2;

  timeSprite.setTextColor(ARMY_GREEN, BG_COLOR);
  timeSprite.setFreeFont(&FreeMono9pt7b);
  timeSprite.setTextDatum(ML_DATUM);
  timeSprite.drawString("IST", startX - 25, 20);

  drawDigit(timeSprite, startX, 2, h / 10, digitW, digitH, thick, ARMY_GREEN);
  drawDigit(timeSprite, startX + digitW + gap, 2, h % 10, digitW, digitH, thick, ARMY_GREEN);

  drawColon(timeSprite, startX + digitW * 2 + gap * 2 + 2, 2, digitH, 6, ARMY_GREEN);

  int colonW = 10;
  int offsetX = startX + digitW * 2 + gap * 2 + colonW + 2;

  drawDigit(timeSprite, offsetX, 2, m / 10, digitW, digitH, thick, ARMY_GREEN);
  drawDigit(timeSprite, offsetX + digitW + gap, 2, m % 10, digitW, digitH, thick, ARMY_GREEN);

  if (use12h) {
    timeSprite.setTextColor(ARMY_AMBER, BG_COLOR);
    timeSprite.setFreeFont(&FreeMono9pt7b);
    timeSprite.setTextDatum(MC_DATUM);
    timeSprite.drawString(isPM ? "PM" : "AM", startX + digitW * 4 + gap * 3 + colonW + 12, 21);
  }

  int londonMin = h * 60 + m - 330;
  if (londonMin < 0) londonMin += 1440;
  if (londonMin >= 1440) londonMin -= 1440;
  int lh = londonMin / 60;
  int lm = londonMin % 60;

  bool lPM = false;
  if (use12h) {
    lPM = lh >= 12;
    if (lh == 0) lh = 12;
    else if (lh > 12) lh -= 12;
  }

  timeSprite.setTextColor(DIM_GREEN, BG_COLOR);
  timeSprite.setFreeFont(&FreeMono9pt7b);
  timeSprite.setTextDatum(ML_DATUM);
  timeSprite.drawString("LON", startX - 25, 48);

  drawDigit(timeSprite, startX, 30, lh / 10, digitW, digitH, thick, DIM_GREEN);
  drawDigit(timeSprite, startX + digitW + gap, 30, lh % 10, digitW, digitH, thick, DIM_GREEN);

  drawColon(timeSprite, startX + digitW * 2 + gap * 2 + 2, 30, digitH, 6, DIM_GREEN);

  drawDigit(timeSprite, offsetX, 30, lm / 10, digitW, digitH, thick, DIM_GREEN);
  drawDigit(timeSprite, offsetX + digitW + gap, 30, lm % 10, digitW, digitH, thick, DIM_GREEN);

  if (use12h) {
    timeSprite.setTextColor(ARMY_AMBER, BG_COLOR);
    timeSprite.setFreeFont(&FreeMono9pt7b);
    timeSprite.setTextDatum(MC_DATUM);
    timeSprite.drawString(lPM ? "PM" : "AM", startX + digitW * 4 + gap * 3 + colonW + 12, 49);
  }

  timeSprite.pushSprite(0, 35);
}

void renderDate(struct tm &t) {
  tft.fillRect(10, 85, 300, 25, BG_COLOR);

  tft.setTextColor(ARMY_AMBER, BG_COLOR);
  tft.setFreeFont(&FreeMono9pt7b);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(dayNames[t.tm_wday], 160, 88);
}

void renderProgressBar(struct tm &t) {
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Boot...");

  use12h = USE_12H_FORMAT;
  Serial.printf("Time format: %s (config)\n", use12h ? "12H" : "24H");

  pinMode(27, OUTPUT);
  ledcSetup(0, 5000, 8);
  ledcAttachPin(27, 0);
  ledcWrite(0, 200);
  Serial.println("Backlight on (PWM 78%)");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    ipAddress = WiFi.localIP().toString();
    Serial.printf("WiFi connected! IP: %s\n", ipAddress.c_str());
    configTime(GMT_OFFSET, DST_OFFSET, NTP_SERVER);
    struct tm t;
    if (getLocalTime(&t, 10000)) {
      ntpSynced = true;
    }
  } else {
    ipAddress = "NO WIFI";
    Serial.printf("WiFi connection failed! Status: %d\n", (int)WiFi.status());
  }

  WiFi.setTxPower(WIFI_POWER_MINUS_1dBm);
  Serial.println("WiFi TX power reduced");

  tft.init();
  Serial.println("TFT init done");
  tft.invertDisplay(false);
  tft.setRotation(1);
  Serial.printf("TFT W=%d H=%d rotation=%d\n", tft.width(), tft.height(), tft.getRotation());
  tft.fillScreen(BG_COLOR);
  delay(100);
  Serial.println("TFT config done");

  timeSprite.createSprite(320, 120);
  Serial.println("Time sprite created");

  drawHeader();
  drawBorder();
  drawFooter();
  Serial.println("UI drawn");

  struct tm t;
  if (getLocalTime(&t)) {
    renderTime(t);
    renderDate(t);
    lastMin = t.tm_min;
  }

  Serial.println("Setup completed successfully!");
}

void loop() {
  struct tm t;
  if (!getLocalTime(&t, 1000)) {
    delay(1000);
    return;
  }

  bool wasSynced = ntpSynced;
  if (!ntpSynced) {
    ntpSynced = true;
  }

  if (ntpSynced != wasSynced) {
    drawHeader();
  }

  if (lastMin == -1 || t.tm_min != lastMin) {
    renderTime(t);
    renderDate(t);
    lastMin = t.tm_min;
  }

  delay(1000);
}