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
  configTime(TIMEZONE_OFFSET_SEC, 0, NTP_SERVER);
  delay(3000);
  time_t now = time(nullptr);
  if (now < NTP_MIN_VALID_TIME) {
    delay(2000);
    now = time(nullptr);
  }
}

void drawStaticElements() {
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

void Update_Display() {
  time_t t;
  time(&t);
  struct tm *timeinfo = localtime(&t);
  if (timeinfo == NULL) {
    Serial.println("Failed to get time");
    return;
  }
  Serial.println("----------The screen time display is about to refresh----------");
  delay(1000);
  Serial.print("Current time: ");
  Serial.println(asctime(timeinfo));

  static char buff1[40];
  static char buff2[40];
  char buffer[80];
  sprintf(buff2, "%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min);
  sprintf(buff1, "%02d-%02d-%04d", timeinfo->tm_mday, timeinfo->tm_mon + 1, timeinfo->tm_year + 1900);

  const char *daysOfWeek[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  const char *dayOfWeek = daysOfWeek[timeinfo->tm_wday];

  Paint_NewImage(ImageBW, EPD_W, EPD_H, 0, WHITE);
  Paint_Clear(WHITE);

  EPD_DrawLine(0, POS_DIVIDER_Y, EPD_W, POS_DIVIDER_Y, BLACK);
  EPD_ShowPicture(POS_TEMP_ICON_X, POS_TEMP_ICON_Y, PIC_TEMP_X, PIC_TEMP_Y, gImage_pic_temp1, WHITE);
  EPD_ShowPicture(POS_HUMI_ICON_X, POS_HUMI_ICON_Y, PIC_HUMI_X, PIC_HUMI_Y, gImage_pic_humi, WHITE);

  EPD_ShowString(POS_DATE_X, POS_DATE_Y, buff1, FONT_DATE_SIZE, BLACK);
  EPD_ShowString(POS_TIME_X, POS_TIME_Y, buff2, FONT_TIME_SIZE, BLACK);
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
