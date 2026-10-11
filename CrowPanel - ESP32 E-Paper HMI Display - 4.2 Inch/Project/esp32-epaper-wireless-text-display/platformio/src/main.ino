#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include "GxEPD2_BW.h"
#include <Fonts/FreeMonoBold24pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include "config.h"

WebServer server(WEB_SERVER_PORT);

GxEPD2_BW<GxEPD2_420_GYE042A87, GxEPD2_420_GYE042A87::HEIGHT> epd(GxEPD2_420_GYE042A87(PIN_CS, PIN_DC, PIN_RES, PIN_BUSY));

struct Row {
  String text;
  int fontSize;
  Row() : text(""), fontSize(FONT_SIZE_MEDIUM) {}
};

struct DisplaySettings {
  static const int MAX_ROWS = 8;
  Row rows[MAX_ROWS];
  bool border;
  bool invertColors;
  DisplaySettings() : border(true), invertColors(false) {}
};

DisplaySettings currentSettings;
unsigned long lastWifiReconnectAttempt = 0;

void epdPower(int state) {
  pinMode(PIN_PWR, OUTPUT);
  digitalWrite(PIN_PWR, state);
}

void epdInit() {
  epd.init(EPD_INIT_BAUD_RATE, EPD_INIT_RESET, EPD_INIT_DELAY, EPD_INIT_PARTIAL);
  epd.setRotation(0);
  epd.setTextColor(currentSettings.invertColors ? GxEPD_WHITE : GxEPD_BLACK);
  epd.setFullWindow();
}

void setFontSize(int size) {
  switch (size) {
    case FONT_SIZE_SMALL:
      epd.setFont(&FreeMonoBold12pt7b);
      break;
    case FONT_SIZE_MEDIUM:
      epd.setFont(&FreeMonoBold18pt7b);
      break;
    case FONT_SIZE_LARGE:
      epd.setFont(&FreeMonoBold24pt7b);
      break;
  }
}

int getRowHeight(int fontSize) {
  switch (fontSize) {
    case FONT_SIZE_SMALL: return ROW_HEIGHT_SMALL;
    case FONT_SIZE_MEDIUM: return ROW_HEIGHT_MEDIUM;
    case FONT_SIZE_LARGE: return ROW_HEIGHT_LARGE;
    default: return ROW_HEIGHT_MEDIUM;
  }
}

void updateDisplay() {
  epdPower(HIGH);
  epdInit();
  epd.fillScreen(currentSettings.invertColors ? GxEPD_BLACK : GxEPD_WHITE);
  if (currentSettings.border) {
    epd.drawRect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, currentSettings.invertColors ? GxEPD_WHITE : GxEPD_BLACK);
  }
  int yPos = getRowHeight(FONT_SIZE_LARGE);
  for (int i = 0; i < currentSettings.MAX_ROWS; i++) {
    if (currentSettings.rows[i].text.length() > 0) {
      setFontSize(currentSettings.rows[i].fontSize);
      epd.setCursor(DISPLAY_MARGIN, yPos);
      epd.print(currentSettings.rows[i].text);
      yPos += getRowHeight(currentSettings.rows[i].fontSize);
    }
  }
  epd.display();
  epd.hibernate();
  epdPower(LOW);
}

bool validateSettings(const DisplaySettings& settings) {
  for (int i = 0; i < settings.MAX_ROWS; i++) {
    if (settings.rows[i].text.length() > MAX_TEXT_LENGTH) {
      return false;
    }
    if (settings.rows[i].fontSize != FONT_SIZE_SMALL &&
        settings.rows[i].fontSize != FONT_SIZE_MEDIUM &&
        settings.rows[i].fontSize != FONT_SIZE_LARGE) {
      return false;
    }
  }
  return true;
}

void handleRoot() {
  File file = LittleFS.open("/index.html", "r");
  if (!file) {
    server.send(500, "text/plain", "Failed to open index.html");
    return;
  }
  server.streamFile(file, "text/html");
  file.close();
}

void handleApiStatus() {
  StaticJsonDocument<1024> doc;
  JsonArray rows = doc.createNestedArray("rows");
  for (int i = 0; i < currentSettings.MAX_ROWS; i++) {
    JsonObject row = rows.createNestedObject();
    row["text"] = currentSettings.rows[i].text;
    row["fontSize"] = currentSettings.rows[i].fontSize;
  }
  doc["border"] = currentSettings.border;
  doc["invertColors"] = currentSettings.invertColors;
  doc["ip"] = WiFi.localIP().toString();
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleApiUpdate() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"No data received\"}");
    return;
  }
  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }
  DisplaySettings newSettings;
  JsonArray rows = doc["rows"];
  int i = 0;
  for (JsonVariant row : rows) {
    if (i >= newSettings.MAX_ROWS) break;
    newSettings.rows[i].text = row["text"].as<String>();
    newSettings.rows[i].fontSize = row["fontSize"].as<int>();
    i++;
  }
  newSettings.border = doc["border"].as<bool>();
  newSettings.invertColors = doc["invertColors"].as<bool>();
  if (!validateSettings(newSettings)) {
    server.send(400, "application/json", "{\"error\":\"Invalid settings\"}");
    return;
  }
  currentSettings = newSettings;
  updateDisplay();
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void displayStartupScreen() {
  epdPower(HIGH);
  epdInit();
  epd.fillScreen(GxEPD_WHITE);
  epd.drawRect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, GxEPD_BLACK);
  epd.setFont(&FreeMonoBold24pt7b);
  epd.setCursor(90, 70);
  epd.print("Wireless");
  epd.setCursor(35, 150);
  epd.print("Info-Display");
  epd.setFont(&FreeMonoBold18pt7b);
  epd.setCursor(45, 230);
  epd.print("Fusion Automate");
  epd.display();
  delay(2000);
}

void displayWiFiStatus(bool connected, String ipAddress = "") {
  epd.fillScreen(GxEPD_WHITE);
  epd.drawRect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, GxEPD_BLACK);
  epd.setFont(&FreeMonoBold18pt7b);
  if (connected) {
    epd.setCursor(60, 70);
    epd.print("WiFi Connected");
    epd.setFont(&FreeMonoBold12pt7b);
    epd.setCursor(80, 150);
    epd.print("IP: " + ipAddress);
  } else {
    epd.setCursor(50, 70);
    epd.print("AP Mode");
    epd.setFont(&FreeMonoBold12pt7b);
    epd.setCursor(30, 150);
    epd.print("Connect to AP");
  }
  epd.setFont(&FreeMonoBold18pt7b);
  epd.setCursor(45, 230);
  epd.print("Fusion Automate");
  epd.display();
  delay(IP_DISPLAY_DURATION);
}

void setupOTA() {
  ArduinoOTA.setHostname(DEVICE_NAME);
  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    Serial.println("OTA Start: " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("OTA End");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error[%u]\n", error);
  });
  ArduinoOTA.begin();
}

void checkWiFiReconnect() {
  if (WiFi.status() != WL_CONNECTED) {
    unsigned long now = millis();
    if (now - lastWifiReconnectAttempt > WIFI_RECONNECT_INTERVAL) {
      lastWifiReconnectAttempt = now;
      Serial.println("Reconnecting WiFi...");
      WiFi.reconnect();
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting...");

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
    return;
  }

  displayStartupScreen();

  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  bool connected = wm.autoConnect(DEVICE_NAME, AP_PASSWORD);

  if (connected) {
    Serial.println("WiFi connected");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    displayWiFiStatus(true, WiFi.localIP().toString());
  } else {
    Serial.println("WiFi failed, AP mode active");
    displayWiFiStatus(false);
  }

  epd.fillScreen(GxEPD_WHITE);
  epd.hibernate();
  epdPower(LOW);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/update", HTTP_POST, handleApiUpdate);
  server.begin();
  Serial.println("Web server started");

  setupOTA();

  currentSettings.invertColors = false;
  updateDisplay();

  Serial.println("Setup completed");
}

void loop() {
  server.handleClient();
  ArduinoOTA.handle();
  checkWiFiReconnect();
  delay(LOOP_DELAY);
}
