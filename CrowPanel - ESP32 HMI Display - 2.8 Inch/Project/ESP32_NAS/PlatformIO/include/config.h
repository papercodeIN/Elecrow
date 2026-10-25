#ifndef CONFIG_H
#define CONFIG_H

// ============================================================
// Elecrow 2.8" ESP32 Solo Miner LCD Display - Pin Definitions
// Verified from factory source code
// ============================================================

// --- SD Card (VSPI bus) ---
#define SD_MOSI     23
#define SD_MISO     19
#define SD_SCK      18
#define SD_CS       5

// --- TFT Display (ILI9341V, 320x240, 320 cols x 240 rows) ---
// TFT_eSPI handles these via build flags in platformio.ini (USE_HSPI_PORT)
// TFT uses HSPI (SPI2) bus: MOSI=13, SCLK=14, CS=15, DC=2, RST=-1, MISO=12
// TOUCH_CS=33 (FT5x06 touch controller, separate bus)
// Backlight control
#define TFT_BL      27

// --- Other pins ---
#define SPEAKER_DAC 26
#define LED_IO      25

// ============================================================
// WiFi Configuration
// ============================================================
// AP mode is open (no password) — user connects to set up home WiFi
#define AP_SSID               "NAS-Setup"
#define WIFI_HOSTNAME       "esp32-nas"
#define MAX_WIFI_FAIL       10   // non-blocking reconnect attempts before AP fallback

// Default WiFi credentials (optional — secrets.h is gitignored)
#if __has_include("secrets.h")
#include "secrets.h"
#define HAS_DEFAULT_CREDENTIALS 1
#else
#define DEFAULT_SSID ""
#define DEFAULT_PASS ""
#define HAS_DEFAULT_CREDENTIALS 0
#endif

// NVS Preferences keys
#define PREF_NAMESPACE  "nas_wifi"
#define PREF_SSID       "ssid"
#define PREF_PASS       "pass"

// ============================================================
// Server Configuration
// ============================================================
#define HTTP_PORT       80

// ============================================================
// Timing (milliseconds)
// ============================================================
#define SD_POLL_INTERVAL    5000
#define WIFI_RETRY_INTERVAL 10000
#define WIFI_CONNECT_TIMEOUT 60000
#define SD_RETRY_ANIM_INTERVAL 500   // localized SD-fault animation tick (small area, no flicker)

#endif // CONFIG_H