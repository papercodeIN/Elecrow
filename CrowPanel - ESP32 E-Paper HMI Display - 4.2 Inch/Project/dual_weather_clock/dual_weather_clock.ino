#include "networking.h"

void syncTimeFromNTP();
void drawStaticElements();

#include <WiFi.h>
#include <HTTPClient.h>
#include <Arduino_JSON.h>
#include "EPD.h"
#include "pic.h"
#include "config.h"

#include <time.h>

#include "EPD_Init.h"
#define EPD_IMAGE_BUFFER_SIZE ((EPD_W * EPD_H) / 8)
static_assert(EPD_IMAGE_BUFFER_SIZE <= 15000, "EPD_IMAGE_BUFFER_SIZE exceeds available memory!");
uint8_t ImageBW[EPD_IMAGE_BUFFER_SIZE];

String jsonBuffer;
int httpResponseCode;
JSONVar myObject;

String temperature;
String humidity;
String city_js;

String serverPath = "http://api.openweathermap.org/data/2.5/weather?q=" WEATHER_CITY "," WEATHER_COUNTRY_CODE "&APPID=" OPENWEATHERMAP_API_KEY "&units=metric";

void syncTimeFromNTP() {
  configTime(0, 0, NTP_SERVER);
  delay(3000);
  time_t now = time(nullptr);
  if (now < NTP_MIN_VALID_TIME) {
    delay(2000);
    now = time(nullptr);
  }
}

void drawStaticElements() {
  EPD_DrawLine(POS_DIVIDER_X, POS_CLOCK_DIVIDER_TOP, POS_DIVIDER_X, POS_CLOCK_DIVIDER_BOTTOM, BLACK);
  EPD_DrawLine(0, POS_DIVIDER_Y, EPD_W, POS_DIVIDER_Y, BLACK);
  EPD_ShowPicture(POS_TEMP_ICON_X, POS_TEMP_ICON_Y, PIC_TEMP_X, PIC_TEMP_Y, gImage_pic_temp1, WHITE);
  EPD_ShowPicture(POS_HUMI_ICON_X, POS_HUMI_ICON_Y, PIC_HUMI_X, PIC_HUMI_Y, gImage_pic_humi, WHITE);
  EPD_Display(ImageBW);
  EPD_Update();
}

void setup() {
  Serial.begin(115200);

  pinMode(DISPLAY_POWER_PIN, OUTPUT);
  digitalWrite(DISPLAY_POWER_PIN, HIGH);

  if (!connectWiFiWithBackoff(WIFI_MAX_RETRIES)) {
    while (true) {
      delay(5000);
      connectWiFiWithBackoff(WIFI_MAX_RETRIES);
    }
  }

  syncTimeFromNTP();
  js_analysis();

  EPD_GPIOInit();
  EPD_Init();
  EPD_Clear();
  EPD_Update();

  Paint_NewImage(ImageBW, EPD_W, EPD_H, 0, WHITE);
  Paint_Clear(WHITE);
  EPD_Display(ImageBW);
  EPD_Clear_R26H(ImageBW);
  EPD_Update();
  drawStaticElements();
  Update_Display();
}

void loop() {
  static unsigned long lastWeatherUpdate = 0;
  static unsigned long lastFullRefresh = 0;
  unsigned long now = millis();

  ensureWiFiConnected();
  syncTimeFromNTP();

  if (now - lastFullRefresh >= FULL_REFRESH_INTERVAL_MS) {
    EPD_Clear();
    EPD_Update();
    Paint_NewImage(ImageBW, EPD_W, EPD_H, 0, WHITE);
    Paint_Clear(WHITE);
    drawStaticElements();
    lastFullRefresh = now;
  }

  if (now - lastWeatherUpdate >= WEATHER_UPDATE_INTERVAL_MS) {
    jsonBuffer = httpGETRequest(serverPath.c_str());
    js_analysis();
    lastWeatherUpdate = now;
  }

  Update_Display();
  delay(UPDATE_INTERVAL_MS);
}

void clear_all() {
  EPD_Clear();
  Paint_NewImage(ImageBW, EPD_W, EPD_H, 0, WHITE);
  Paint_Clear(WHITE);
  EPD_Display_Part(0, 0, EPD_W, EPD_H, ImageBW);
}

void formatTime12(struct tm *timeinfo, char *timeBuf, char *ampmBuf) {
  int hour = timeinfo->tm_hour;
  if (hour == 0) {
    sprintf(ampmBuf, "AM");
    sprintf(timeBuf, "12:%02d", timeinfo->tm_min);
  } else if (hour < 12) {
    sprintf(ampmBuf, "AM");
    sprintf(timeBuf, "%02d:%02d", hour, timeinfo->tm_min);
  } else if (hour == 12) {
    sprintf(ampmBuf, "PM");
    sprintf(timeBuf, "12:%02d", timeinfo->tm_min);
  } else {
    sprintf(ampmBuf, "PM");
    sprintf(timeBuf, "%02d:%02d", hour - 12, timeinfo->tm_min);
  }
}

void Update_Display() {
  time_t t;
  time(&t);

  time_t t_ist = t + (time_t)TIMEZONE_IST_OFFSET_SEC;
  struct tm ist_info;
  gmtime_r(&t_ist, &ist_info);
  ist_info.tm_wday = (int)((t_ist / 86400 + 4) % 7);

  time_t t_ukt = t + (time_t)TIMEZONE_LONDON_OFFSET_SEC;
  struct tm ukt_info;
  gmtime_r(&t_ukt, &ukt_info);
  ukt_info.tm_wday = (int)((t_ukt / 86400 + 4) % 7);

  Serial.println("----------The screen time display is about to refresh----------");
  delay(1000);
  Serial.print("IST: ");
  Serial.println(asctime(&ist_info));
  Serial.print("UKT: ");
  Serial.println(asctime(&ukt_info));

  static char dateBuf[40];
  static char istTimeBuf[20];
  static char istAmpmBuf[4];
  static char uktTimeBuf[20];
  static char uktAmpmBuf[4];
  char buffer[80];

  sprintf(dateBuf, "%02d-%02d-%04d", ist_info.tm_mday, ist_info.tm_mon + 1, ist_info.tm_year + 1900);
  formatTime12(&ist_info, istTimeBuf, istAmpmBuf);
  formatTime12(&ukt_info, uktTimeBuf, uktAmpmBuf);

  const char *daysOfWeek[] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
  const char *dayOfWeek = daysOfWeek[ist_info.tm_wday];

  Paint_NewImage(ImageBW, EPD_W, EPD_H, 0, WHITE);
  Paint_Clear(WHITE);

  EPD_DrawLine(POS_DIVIDER_X, POS_CLOCK_DIVIDER_TOP, POS_DIVIDER_X, POS_CLOCK_DIVIDER_BOTTOM, BLACK);
  EPD_DrawLine(0, POS_DIVIDER_Y, EPD_W, POS_DIVIDER_Y, BLACK);
  EPD_ShowPicture(POS_TEMP_ICON_X, POS_TEMP_ICON_Y, PIC_TEMP_X, PIC_TEMP_Y, gImage_pic_temp1, WHITE);
  EPD_ShowPicture(POS_HUMI_ICON_X, POS_HUMI_ICON_Y, PIC_HUMI_X, PIC_HUMI_Y, gImage_pic_humi, WHITE);

  EPD_ShowString(POS_DATE_X, POS_DATE_Y, dateBuf, FONT_DATE_SIZE, BLACK);

  EPD_ShowString(POS_IST_LABEL_X, POS_IST_LABEL_Y, "IST", FONT_LABEL_SIZE, BLACK);
  EPD_ShowString(POS_TIME_IST_X, POS_TIME_IST_Y, istTimeBuf, FONT_TIME_SIZE, BLACK);
  EPD_ShowString(POS_AMPM_IST_X, POS_AMPM_IST_Y, istAmpmBuf, FONT_AMPM_SIZE, BLACK);

  EPD_ShowString(POS_UKT_LABEL_X, POS_UKT_LABEL_Y, "UKT", FONT_LABEL_SIZE, BLACK);
  EPD_ShowString(POS_TIME_LONDON_X, POS_TIME_LONDON_Y, uktTimeBuf, FONT_TIME_SIZE, BLACK);
  EPD_ShowString(POS_AMPM_LONDON_X, POS_AMPM_LONDON_Y, uktAmpmBuf, FONT_AMPM_SIZE, BLACK);

  EPD_ShowString(POS_DAY_X, POS_DAY_Y, dayOfWeek, FONT_DAY_SIZE, BLACK);

  memset(buffer, 0, sizeof(buffer));
  snprintf(buffer, sizeof(buffer), "%s C", temperature.c_str());
  EPD_ShowString(POS_TEMP_TEXT_X, POS_TEMP_TEXT_Y, buffer, FONT_WEATHER_SIZE, BLACK);
  memset(buffer, 0, sizeof(buffer));
  snprintf(buffer, sizeof(buffer), "%s %%", humidity.c_str());
  EPD_ShowString(POS_HUMI_TEXT_X, POS_HUMI_TEXT_Y, buffer, FONT_WEATHER_SIZE, BLACK);

  Serial.println("* * * * * * The screen time display has been refreshed * * * * * *");

  EPD_Display(ImageBW);
  EPD_Update_Part();
}

void js_analysis() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  uint8_t attempts = 0;
  while (attempts < WEATHER_FETCH_MAX_RETRIES) {
    jsonBuffer = httpGETRequest(serverPath.c_str());
    if (httpResponseCode != 200) {
      attempts++;
      delay(1000);
      continue;
    }

    myObject = JSON.parse(jsonBuffer);
    if (JSON.typeof(myObject) == "undefined") {
      attempts++;
      delay(1000);
      continue;
    }

    temperature = JSON.stringify(myObject["main"]["temp"]);
    humidity = JSON.stringify(myObject["main"]["humidity"]);
    city_js = JSON.stringify(myObject["name"]);
    return;
  }
}

String httpGETRequest(const char* serverName) {
  WiFiClient client;
  HTTPClient http;
  String payload = "{}";

  http.begin(client, serverName);
  httpResponseCode = http.GET();

  if (httpResponseCode == 200) {
    payload = http.getString();
  } else {
    Serial.print("HTTP error: ");
    Serial.println(httpResponseCode);
  }

  http.end();
  return payload;
}
