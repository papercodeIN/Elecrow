#pragma once
// ===========================================================================
// config.h - board + feature configuration
//
// Board: Elecrow CrowPanel 2.8" ESP32 CYD (ESP32-WROOM-32-N4, 4 MB flash,
//        no PSRAM), ILI9341V 240x320 SPI TFT, resistive touch, speaker on DAC.
//
// TFT pins (MOSI/SCLK/CS/DC/RST/BL/TOUCH_CS) are compile-time constants for
// TFT_eSPI and are set in platformio.ini build_flags, NOT here.
// This file holds everything the application itself needs.
// ===========================================================================

// ----------------------------- Board pins ---------------------------------
#define PIN_BACKLIGHT      27   // TFT backlight, PWM dimmed (LEDC)
#define PIN_SPEAKER        26   // Speaker PH2.0-2P connector, driven via LEDC PWM
// Touch uses the shared SPI bus, chip select defined as TOUCH_CS=33 in build flags.

// ----------------------------- LEDC channels ------------------------------
#define LEDC_CH_BACKLIGHT   0
#define LEDC_CH_SPEAKER     1
#define LEDC_BL_FREQ        5000   // Hz, backlight PWM (above audible range)
#define LEDC_BL_RES         8      // bit
#define LEDC_SPK_RES        10     // bit, 50% duty = 512

// Backlight brightness levels (duty out of 255), cycled by single tap.
static const uint8_t kBrightnessLevels[3] = { 255, 140, 64 };

// ----------------------------- BLE ----------------------------------------
// Nordic UART Service (NUS): works with nRF Connect, Web BLE, bleak, Tasker
// BLE plugins and every serial-over-BLE helper library.
#define BLE_DEVICE_NAME     "AA Nav Display"
#define BLE_SERVICE_UUID    "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_CHAR_RX_UUID    "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // phone -> display (Write)
#define BLE_CHAR_TX_UUID    "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // display -> phone (Notify)

#define BLE_LINE_MAX        384    // reassembly buffer for one JSON line

// ----------------------------- Timing -------------------------------------
#define SIGNAL_LOST_MS      10000  // no message for this long in NAV -> banner
#define TOUCH_POLL_MS       8
#define DOUBLE_TAP_MS       400    // max gap between taps for night-mode toggle
#define FLASH_PERIOD_MS     400    // TURN NOW flash period

// ----------------------------- Urgency thresholds -------------------------
// Computed on the display from the `d` field (meters to maneuver) so prompts
// fire even between phone updates.
#define URGENT_NEAR_M       150    // amber highlight + single chime
#define URGENT_NOW_M        40     // flashing red + urgent chime pattern

// ----------------------------- Chime patterns -----------------------------
// freq Hz, then {on_ms, off_ms} steps, terminated by {0,0}. Non-blocking.
static const uint16_t kChimeNearFreq   = 2100;
static const uint16_t kChimeNowFreq    = 2600;
static const uint16_t kChimeArriveFreq = 1500;

// ----------------------------- UI layout (320x240 landscape) --------------
#define UI_WIDTH            320
#define UI_HEIGHT           240
#define STATUS_BAR_H        22
#define ARROW_PANEL_W       120
#define ARROW_PANEL_H       190
#define ARROW_PANEL_X       0
#define ARROW_PANEL_Y       STATUS_BAR_H
#define PROGRESS_Y          230
#define PROGRESS_H          8
#define BANNER_Y            212
#define BANNER_H            15
