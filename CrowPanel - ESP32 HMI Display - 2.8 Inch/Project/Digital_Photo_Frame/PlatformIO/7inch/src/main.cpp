#include <Arduino.h>
#include <WiFi.h>
#include <Arduino_GFX_Library.h>

// CrowPanel 7.0-inch ST7262 RGB panel, official Elecrow Arduino_GFX example pinout.
static Arduino_ESP32RGBPanel *rgbPanel = new Arduino_ESP32RGBPanel(
    40, 41, 39, 42,
    45, 48, 47, 21, 14,
    5, 6, 7, 15, 16, 4,
    8, 3, 46, 9, 1,
    0, 8, 4, 8,
    0, 8, 4, 8,
    1, 16000000, false,
    0, 0, 0);
static Arduino_RGB_Display display(800, 480, rgbPanel, 0, true);

static constexpr int BACKLIGHT_PIN = 2;
static constexpr uint16_t COLOR_BG = 0x10A2;
static constexpr uint16_t COLOR_ACCENT = 0x2D7F;

void setup() {
  Serial.begin(115200);
  pinMode(BACKLIGHT_PIN, OUTPUT);
  digitalWrite(BACKLIGHT_PIN, HIGH);
  display.begin();
  display.fillScreen(COLOR_BG);
  display.fillRect(0, 0, 800, 72, COLOR_ACCENT);
  display.setTextColor(0xFFFF);
  display.setTextSize(3);
  display.setCursor(28, 24);
  display.print("ELECROW PHOTO FRAME");
  display.setTextSize(2);
  display.setCursor(30, 130);
  display.print("CrowPanel 7-inch RGB display ready");
  display.setTextSize(1);
  display.setCursor(30, 190);
  display.print("800 x 480  |  ST7262  |  RGB parallel");
  Serial.println("CrowPanel 7-inch RGB display initialized");
}

void loop() {
  delay(1000);
}
