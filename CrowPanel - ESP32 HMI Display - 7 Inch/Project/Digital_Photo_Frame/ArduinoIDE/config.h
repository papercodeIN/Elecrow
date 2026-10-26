#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================
// Elecrow 2.8" ESP32 Solo Miner LCD Display - Hardware Pinout
// Screen: 320x240 ILI9341V TFT (Landscape)
// ============================================================

// --- MicroSD Card (VSPI bus / SPI3) ---
#define SD_MOSI             23
#define SD_MISO             19
#define SD_SCK              18
#define SD_CS               5
#define SD_SPI_SPEED        4000000     // 4 MHz stable SPI clock for SD

// --- TFT Display (HSPI bus / SPI2 via TFT_eSPI build flags) ---
// MOSI=13, SCLK=14, CS=15, DC=2, RST=-1, MISO=12
#define TFT_BL              27          // Backlight control (active HIGH)
#define TOUCH_CS            33          // Resistive/Capacitive touch CS
#define SPEAKER_DAC         26          // Internal DAC speaker output
#define LED_IO              25          // Board LED / IO

#define SCREEN_WIDTH        320
#define SCREEN_HEIGHT       240

// ============================================================
// WiFi Configuration
// ============================================================
#define AP_SSID             "PhotoFrame-Setup"
#define AP_PASS             ""          // Open AP for captive portal
#define WIFI_HOSTNAME       "esp32-photoframe"
#define MAX_WIFI_FAIL       10          // Connect attempts before fallback
#define WIFI_CONNECT_TIMEOUT 60000      // 60s timeout
#define WIFI_RETRY_INTERVAL  15000      // Retry STA reconnect every 15s

#if __has_include("secrets.h")
#include "secrets.h"
#define HAS_DEFAULT_CREDENTIALS 1
#else
#define DEFAULT_SSID ""
#define DEFAULT_PASS ""
#define HAS_DEFAULT_CREDENTIALS 0
#endif

// ============================================================
// NVS Storage & Preferences Keys
// ============================================================
#define PREF_WIFI_NS        "pf_wifi"
#define PREF_SETTINGS_NS    "pf_config"

#define PREF_KEY_SSID       "ssid"
#define PREF_KEY_PASS       "pass"

#define PREF_KEY_INTERVAL   "interval"    // uint32_t (seconds)
#define PREF_KEY_ORDER      "order"       // uint8_t (0: Sequential, 1: Shuffle)
#define PREF_KEY_FIT        "fit"         // uint8_t (0: Letterbox, 1: Crop/Fill, 2: Auto)
#define PREF_KEY_TRANSITION "trans"       // uint8_t (0: Instant, 1..11: Effects, 12: Auto Random)
#define PREF_KEY_PAUSED     "paused"      // uint8_t (0: Playing, 1: Paused)

// ============================================================
// Slideshow Defaults
// ============================================================
#define DEFAULT_INTERVAL_SEC 5
#define DEFAULT_ORDER_MODE   0           // 0 = Sequential, 1 = Shuffle
#define DEFAULT_FIT_MODE     2           // 2 = Auto (portrait full-height; landscape fill screen)
#define DEFAULT_TRANSITION   12          // 12 = Auto (Random transition effect every slide)
#define MAX_PHOTOS_PLAYLIST  512         // Maximum photos tracked in playlist
#define PHOTO_DIR            "/photos"   // Dedicated folder for photos on SD

// ============================================================
// Web Server & Polling Timings
// ============================================================
#define HTTP_PORT           80
#define SD_POLL_INTERVAL    5000        // Check SD status every 5s
#define SD_RETRY_ANIM_MS    400         // Localized SD error blink

#endif // CONFIG_H

