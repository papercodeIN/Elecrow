#include <WiFi.h>
#include <TFT_eSPI.h>
#include <time.h>
#include "secrets.h"

const char* NTP_SERVER = "pool.ntp.org";
const long  GMT_OFFSET = 19800;
const int   DST_OFFSET = 0;

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite timeSprite  = TFT_eSprite(&tft);
TFT_eSprite dateSprite  = TFT_eSprite(&tft);
TFT_eSprite colonSprite = TFT_eSprite(&tft);

#define BG_COLOR      0x0000
#define ARMY_GREEN    0x2E05
#define ARMY_AMBER    0xFCA0
#define DIM_GREEN     0x1B02
#define GRID_COLOR    0x0901
#define HEADER_BG     0x1B02

const uint8_t SEGMENT_MAP[10] = {
  0x3F, 0x06, 0x5B, 0x4F, 0x66,
  0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

const char* dayNames[] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
const char* monthNames[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

int lastSec = -1;
bool colonVisible = true;
bool ntpSynced = false;

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

void drawDigit(TFT_eSprite &spr, int x, int y, int digit, int w, int h, int thick, uint32_t onColor, uint32_t offColor) {
  uint8_t seg = SEGMENT_MAP[digit];
  int halfH = h / 2;

  drawHSegment(spr, x, y, w, thick, (seg & 0x01) ? onColor : offColor);
  drawVSegment(spr, x + w - thick, y, halfH, thick, (seg & 0x02) ? onColor : offColor);
  drawVSegment(spr, x + w - thick, y + halfH, halfH, thick, (seg & 0x04) ? onColor : offColor);
  drawHSegment(spr, x, y + h - thick, w, thick, (seg & 0x08) ? onColor : offColor);
  drawVSegment(spr, x, y + halfH, halfH, thick, (seg & 0x10) ? onColor : offColor);
  drawVSegment(spr, x, y, halfH, thick, (seg & 0x20) ? onColor : offColor);
  drawHSegment(spr, x, y + halfH - thick / 2, w, thick, (seg & 0x40) ? onColor : offColor);
}

void drawColon(TFT_eSprite &spr, int x, int y, int h, int dotSize, uint32_t color) {
  int quarter = h / 4;
  spr.fillRect(x, y + quarter - dotSize / 2, dotSize, dotSize, color);
  spr.fillRect(x, y + 3 * quarter - dotSize / 2, dotSize, dotSize, color);
}

void drawScanlines() {
  for (int y = 0; y < 320; y += 3) {
    tft.drawFastHLine(0, y, 240, GRID_COLOR);
  }
}

void drawHeader() {
  tft.fillRect(0, 0, 240, 28, HEADER_BG);
  tft.drawFastHLine(0, 28, 240, ARMY_GREEN);
  tft.drawFastHLine(0, 29, 240, ARMY_GREEN);

  tft.setTextColor(ARMY_GREEN, HEADER_BG);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("[ MILITARY TIME ]", 120, 14);
}

void drawStatusBar() {
  tft.fillRect(0, 295, 240, 25, HEADER_BG);
  tft.drawFastHLine(0, 295, 240, ARMY_GREEN);
  tft.drawFastHLine(0, 296, 240, ARMY_GREEN);

  tft.setTextColor(DIM_GREEN, HEADER_BG);
  tft.setFreeFont(&FreeMono9pt7b);
  tft.setTextDatum(ML_DATUM);
  tft.drawString("SYS:ACTIVE", 8, 308);

  tft.setTextDatum(MR_DATUM);
  if (ntpSynced) {
    tft.setTextColor(ARMY_GREEN, HEADER_BG);
    tft.drawString("NTP:SYNC", 232, 308);
  } else {
    tft.setTextColor(ARMY_AMBER, HEADER_BG);
    tft.drawString("NTP:WAIT", 232, 308);
  }
}

void drawBorder() {
  tft.drawRect(0, 30, 240, 265, DIM_GREEN);
  tft.drawRect(1, 31, 238, 263, DIM_GREEN);
}

void renderTime(struct tm &t) {
  timeSprite.fillSprite(BG_COLOR);

  int digitW = 42;
  int digitH = 70;
  int thick  = 7;
  int gap    = 5;

  int h = t.tm_hour;
  int m = t.tm_min;

  drawDigit(timeSprite, 0, 5, h / 10, digitW, digitH, thick, ARMY_GREEN, GRID_COLOR);
  drawDigit(timeSprite, digitW + gap, 5, h % 10, digitW, digitH, thick, ARMY_GREEN, GRID_COLOR);

  if (colonVisible) {
    drawColon(timeSprite, digitW * 2 + gap * 2 + 2, 5, digitH, 8, ARMY_GREEN);
  } else {
    drawColon(timeSprite, digitW * 2 + gap * 2 + 2, 5, digitH, 8, GRID_COLOR);
  }

  int colonW = 16;
  int offsetX = digitW * 2 + gap * 2 + colonW + 4;

  drawDigit(timeSprite, offsetX, 5, m / 10, digitW, digitH, thick, ARMY_GREEN, GRID_COLOR);
  drawDigit(timeSprite, offsetX + digitW + gap, 5, m % 10, digitW, digitH, thick, ARMY_GREEN, GRID_COLOR);

  timeSprite.pushSprite(5, 60);
}

void renderSeconds(struct tm &t) {
  int s = t.tm_sec;
  tft.setTextColor(ARMY_AMBER, BG_COLOR);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextDatum(MC_DATUM);

  char secBuf[8];
  sprintf(secBuf, ": %02d", s);
  tft.fillRect(170, 140, 65, 22, BG_COLOR);
  tft.drawString(secBuf, 200, 151);
}

void renderDate(struct tm &t) {
  dateSprite.fillSprite(BG_COLOR);

  dateSprite.setTextColor(ARMY_GREEN, BG_COLOR);
  dateSprite.setFreeFont(&FreeSansBold9pt7b);
  dateSprite.setTextDatum(TC_DATUM);

  char dateBuf[32];
  sprintf(dateBuf, "%02d %s %04d", t.tm_mday, monthNames[t.tm_mon], t.tm_year + 1900);
  dateSprite.drawString(dateBuf, 110, 2);

  dateSprite.setTextColor(ARMY_AMBER, BG_COLOR);
  dateSprite.setFreeFont(&FreeMono9pt7b);
  dateSprite.setTextDatum(TC_DATUM);
  dateSprite.drawString(dayNames[t.tm_wday], 110, 26);

  dateSprite.pushSprite(10, 180);
}

void renderProgressBar(struct tm &t) {
  int barY = 230;
  int barH = 8;
  int barW = 220;
  int barX = 10;

  float progress = (t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec) / 86400.0;
  int fillW = (int)(barW * progress);

  tft.drawRect(barX - 1, barY - 1, barW + 2, barH + 2, DIM_GREEN);
  tft.fillRect(barX, barY, fillW, barH, ARMY_GREEN);
  tft.fillRect(barX + fillW, barY, barW - fillW, barH, GRID_COLOR);

  tft.setTextColor(DIM_GREEN, BG_COLOR);
  tft.setFreeFont(&FreeMono9pt7b);
  tft.setTextDatum(TC_DATUM);

  char pctBuf[8];
  sprintf(pctBuf, "%d%%", (int)(progress * 100));
  tft.fillRect(90, 242, 60, 18, BG_COLOR);
  tft.drawString(pctBuf, 120, 244);

  tft.setTextColor(DIM_GREEN, BG_COLOR);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("0000", barX, 262);
  tft.setTextDatum(TR_DATUM);
  tft.drawString("2359", barX + barW, 262);
}

void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(BG_COLOR);
  tft.invertDisplay(false);

  ledcSetup(0, 5000, 8);
  ledcAttachPin(21, 0);
  ledcWrite(0, 200);

  timeSprite.createSprite(230, 80);
  dateSprite.createSprite(220, 50);
  colonSprite.createSprite(16, 70);

  drawHeader();
  drawBorder();
  drawScanlines();
  drawStatusBar();

  tft.setTextColor(ARMY_GREEN, BG_COLOR);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("CONNECTING...", 120, 160);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    configTime(GMT_OFFSET, DST_OFFSET, NTP_SERVER);
    struct tm t;
    if (getLocalTime(&t, 5000)) {
      ntpSynced = true;
    }
  }

  tft.fillRect(2, 32, 236, 261, BG_COLOR);
  drawScanlines();
  drawStatusBar();
}

void loop() {
  struct tm t;
  if (!getLocalTime(&t)) {
    delay(500);
    return;
  }

  if (t.tm_sec != lastSec) {
    colonVisible = !colonVisible;
    renderTime(t);
    renderSeconds(t);

    if (lastSec == -1 || t.tm_min != (lastSec >= 0 ? -1 : 0)) {
      renderDate(t);
      renderProgressBar(t);
    }

    renderProgressBar(t);
    lastSec = t.tm_sec;
  }

  delay(100);
}
