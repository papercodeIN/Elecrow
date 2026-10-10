#include <WiFi.h>
#include <TFT_eSPI.h>
#include <time.h>
#include "secrets.h"

const char* NTP_SERVER = "pool.ntp.org";
const char* TZ_IST = "IST-5:30";
const char* TZ_UK  = "GMT0BST,M3.5.0/1,M10.5.0/2";

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

void drawHeader() {
  tft.fillRect(0, 0, 320, 28, HEADER_BG);
  tft.drawFastHLine(0, 28, 320, ARMY_GREEN);
  tft.drawFastHLine(0, 29, 320, ARMY_GREEN);

  const int headerTextY = 5;

  tft.setFreeFont(&FreeMono9pt7b);
  tft.setTextDatum(TL_DATUM);
  if (ntpSynced) {
    tft.setTextColor(ARMY_GREEN, HEADER_BG);
    tft.drawString("NTP:OK", 6, headerTextY);
  } else {
    tft.setTextColor(ARMY_AMBER, HEADER_BG);
    tft.drawString("NTP:WAIT", 6, headerTextY);
  }

  tft.setTextColor(ARMY_GREEN, HEADER_BG);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("TIME", 170, 14);

  tft.setTextColor(ARMY_AMBER, HEADER_BG);
  tft.setFreeFont(&FreeMono9pt7b);
  tft.setTextDatum(TR_DATUM);
  tft.drawString(use12h ? "12H" : "24H", 314, headerTextY);
}

void drawFooter() {
  tft.fillRect(0, 215, 320, 25, HEADER_BG);
  tft.drawFastHLine(0, 215, 320, ARMY_GREEN);
  tft.drawFastHLine(0, 216, 320, ARMY_GREEN);

  tft.setTextColor(DIM_GREEN, HEADER_BG);
  tft.setFreeFont(&FreeMono9pt7b);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(ipAddress, 160, 228);
}

void drawBorder() {
  tft.drawRect(0, 30, 320, 185, DIM_GREEN);
  tft.drawRect(1, 31, 318, 183, DIM_GREEN);
}

bool getZonedTimes(struct tm &istTime, struct tm &ukTime) {
  time_t now = time(nullptr);
  if (now < 100000) {
    return false;
  }

  setenv("TZ", TZ_IST, 1);
  tzset();
  localtime_r(&now, &istTime);

  setenv("TZ", TZ_UK, 1);
  tzset();
  localtime_r(&now, &ukTime);
  return true;
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

  int digitW = 22;
  int digitH = 38;
  int thick = 4;
  int gap = 3;
  int colonW = 8;
  int totalW = digitW * 4 + gap * 3 + colonW;
  int x = centerX - totalW / 2;
  int y = 61;

  tft.setTextColor(ARMY_AMBER, BG_COLOR);
  tft.setFreeFont(&FreeMono9pt7b);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(label, centerX, 38);

  drawTftDigit(x, y, h / 10, digitW, digitH, thick, color);
  drawTftDigit(x + digitW + gap, y, h % 10, digitW, digitH, thick, color);
  drawTftColon(x + digitW * 2 + gap * 2, y, digitH, 5, color);
  drawTftDigit(x + digitW * 2 + gap * 2 + colonW, y, t.tm_min / 10, digitW, digitH, thick, color);
  drawTftDigit(x + digitW * 3 + gap * 3 + colonW, y, t.tm_min % 10, digitW, digitH, thick, color);

  if (use12h) {
    tft.setTextColor(ARMY_AMBER, BG_COLOR);
    tft.setFreeFont(&FreeMono9pt7b);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(periodBuf, centerX, 112);
  }
}

void renderTime(struct tm &istTime, struct tm &ukTime) {
  tft.fillRect(2, 32, 316, 95, BG_COLOR);
  tft.drawFastVLine(160, 38, 82, GRID_COLOR);
  drawTimeColumn(80, "IST", istTime, ARMY_GREEN);
  drawTimeColumn(240, "UKT", ukTime, DIM_GREEN);
}

void renderDate(struct tm &t) {
  tft.fillRect(10, 130, 300, 35, BG_COLOR);

  tft.setTextColor(ARMY_AMBER, BG_COLOR);
  tft.setFreeFont(&FreeMono9pt7b);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(dayNames[t.tm_wday], 160, 132);

  tft.setTextColor(ARMY_GREEN, BG_COLOR);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextDatum(TC_DATUM);

  char dateBuf[32];
  sprintf(dateBuf, "%02d %s %04d", t.tm_mday, monthNames[t.tm_mon], t.tm_year + 1900);
  tft.drawString(dateBuf, 160, 152);
}

void renderProgressBar(struct tm &t) {
  int barY = 170;
  int barH = 8;
  int barW = 280;
  int barX = 20;

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
  tft.fillRect(130, 182, 60, 16, BG_COLOR);
  tft.drawString(pctBuf, 160, 185);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Boot...");

  use12h = USE_12H_FORMAT;
  Serial.printf("Time format: %s (config)\n", use12h ? "12H" : "24H");

  pinMode(27, OUTPUT);
  analogWrite(27, 200);
  Serial.println("Backlight on (PWM 78%)");

  Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    ipAddress = WiFi.localIP().toString();
    Serial.printf("WiFi connected! IP: %s\n", ipAddress.c_str());
    configTime(0, 0, NTP_SERVER);
    Serial.println("Waiting for NTP sync...");
    struct tm t;
    if (getLocalTime(&t, 10000)) {
      ntpSynced = true;
      Serial.println("NTP sync OK!");
    } else {
      Serial.println("NTP sync failed!");
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

  timeSprite.createSprite(320, 100);
  Serial.println("Time sprite created");

  drawHeader();
  drawBorder();
  drawFooter();
  Serial.println("UI drawn");

  struct tm istTime;
  struct tm ukTime;
  if (getZonedTimes(istTime, ukTime)) {
    renderTime(istTime, ukTime);
    renderDate(istTime);
    renderProgressBar(istTime);
    lastMin = istTime.tm_min;
  }

  Serial.println("Setup completed successfully!");
}

void loop() {
  struct tm istTime;
  struct tm ukTime;
  if (!getZonedTimes(istTime, ukTime)) {
    Serial.println("loop: waiting for time...");
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

  if (lastMin == -1 || istTime.tm_min != lastMin) {
    Serial.printf("IST: %02d:%02d UK: %02d:%02d\n", istTime.tm_hour, istTime.tm_min, ukTime.tm_hour, ukTime.tm_min);
    renderTime(istTime, ukTime);
    renderDate(istTime);
    renderProgressBar(istTime);
    lastMin = istTime.tm_min;
  }

  delay(1000);
}
