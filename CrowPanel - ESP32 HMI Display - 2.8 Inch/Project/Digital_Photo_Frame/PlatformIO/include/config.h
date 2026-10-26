#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================
// Display hardware pinout (selected per PlatformIO environment)
// ============================================================
// Elecrow 2.8" ESP32 Solo Miner LCD Display: 320x240 ILI9341V.

// --- MicroSD Card (VSPI bus / SPI3) ---
#define SD_MOSI             23
#define SD_MISO             19
#define SD_SCK              18
#define SD_CS               5
#define SD_SPI_SPEED        4000000     // 4 MHz stable SPI clock for SD

// --- TFT Display (HSPI bus / SPI2 via TFT_eSPI build flags) ---
// MOSI=13, SCLK=14, CS=15, DC=2, RST=-1, MISO=12
#define TFT_BL              27          // Backlight control (PWM via LEDC)
#define TFT_BL_LEDC_CH      0           // LEDC channel 0 for backlight PWM
#define TFT_BL_LEDC_FREQ    1000        // 1 kHz PWM frequency (flicker-free)
#define TFT_BL_LEDC_RES     8           // 8-bit resolution (0–255)
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
#define MAX_WIFI_FAIL       3           // Connect attempts before fallback (was 10)
#define WIFI_CONNECT_TIMEOUT 15000      // 15s timeout (was 60s)
#define WIFI_RETRY_INTERVAL  5000       // Retry STA reconnect every 5s (was 15s)

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
#define PREF_KEY_BRIGHTNESS "bright"      // uint8_t (0–255, default 255 = full)

// ============================================================
// Slideshow Defaults
// ============================================================
#define DEFAULT_INTERVAL_SEC 5
#define DEFAULT_ORDER_MODE   0           // 0 = Sequential, 1 = Shuffle
#define DEFAULT_FIT_MODE     2           // 2 = Auto (portrait full-height; landscape fill screen)
#define DEFAULT_TRANSITION   12          // 12 = Auto (Random transition effect every slide)
#define DEFAULT_BRIGHTNESS   255         // 255 = Full brightness (max)
#define MAX_PHOTOS_PLAYLIST  512         // Maximum photos tracked in playlist
#define PHOTO_DIR            "/photos"   // Dedicated folder for photos on SD

// ============================================================
// Web Server & Polling Timings
// ============================================================
#define HTTP_PORT           80
#define SD_POLL_INTERVAL    1000        // Check SD status every 1s (fast re-mount after boot)
#define SD_RETRY_ANIM_MS    400         // Localized SD error blink

// ============================================================
// Transition Animation Timings
// ============================================================
#define TRANS_WIPE_STEP       16
#define TRANS_WIPE_DELAY_MS   4
#define TRANS_BLINDS_STEPS    20
#define TRANS_BLINDS_DELAY_MS 4
#define TRANS_CURTAIN_STEP    8
#define TRANS_CURTAIN_DELAY_MS 6
#define TRANS_BOX_STEPS       60
#define TRANS_BOX_STEP_INC    4
#define TRANS_BOX_DELAY_MS    6
#define TRANS_CHECKER_SIZE    40
#define TRANS_CHECKER_DELAY_MS 8
#define TRANS_CHECKER_PAUSE_MS 15
#define TRANS_FADE_PASSES     4
#define TRANS_FADE_DELAY_MS   12

// ============================================================
// Watchdog Timer
// ============================================================
#define WDT_TIMEOUT_MS      8000        // Watchdog timeout (8 seconds)
#define WDT_FEED_INTERVAL   1000        // Feed watchdog every 1s

// ============================================================
// Logging Levels (compile-time filtering)
// ============================================================
// 0 = None, 1 = Error, 2 = Warn, 3 = Info, 4 = Debug
#define LOG_LEVEL           3

#define LOG_ERROR(...)      do { if (LOG_LEVEL >= 1) Serial.printf("[ERR] " __VA_ARGS__); } while(0)
#define LOG_WARN(...)       do { if (LOG_LEVEL >= 2) Serial.printf("[WRN] " __VA_ARGS__); } while(0)
#define LOG_INFO(...)       do { if (LOG_LEVEL >= 3) Serial.printf("[INF] " __VA_ARGS__); } while(0)
#define LOG_DEBUG(...)      do { if (LOG_LEVEL >= 4) Serial.printf("[DBG] " __VA_ARGS__); } while(0)

#endif // CONFIG_H

