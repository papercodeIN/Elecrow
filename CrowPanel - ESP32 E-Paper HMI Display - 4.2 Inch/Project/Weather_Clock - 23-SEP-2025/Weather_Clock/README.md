# Weather Clock ESP32 E-Paper Project

Displays time, temperature, and humidity on a CrowPanel ESP32 E-Paper 4.2-inch HMI display using an ESP32-S3. Weather data is fetched from OpenWeatherMap.

## Hardware

- Elecrow CrowPanel ESP32 E-Paper HMI Display 4.2-inch
- Controller: SSD1683
- Resolution: 400x300 (1bpp)
- SPI: bit-banged via GPIO macros

## Arduino IDE Setup

### Board Package

Install the ESP32 board package via **Boards Manager** (`esp32` by Espressif Systems). No specific version is required; use the latest stable release.

### Board Settings

| Setting | Value |
|---|---|
| **Board** | `ESP32S3 Dev Module` (under esp32) |
| **Partition Scheme** | `Huge APP (3MB No OTA/1MB SPIFFS)` |
| **PSRAM** | `OPI PSRAM` |
| **Port** | Select the COM port detected when the board is connected via USB |

All other settings (upload speed, flash mode, CPU frequency) can remain at their defaults.

### Required Libraries

| Library | Source |
|---|---|
| `WiFi` | Bundled with ESP32 board package |
| `HTTPClient` | Bundled with ESP32 board package |
| `Arduino_JSON` | Install via Arduino Library Manager |

No additional third-party libraries are needed. The EPD driver files (`EPD.h`, `EPD_Init.h`, `spi.h`, etc.) are included directly in the project.

## Project Setup

1. Clone the repository.
2. Edit `config.h` with your credentials:
   ```cpp
   #define WIFI_SSID "your_wifi"
   #define WIFI_PASSWORD "your_password"
   #define OPENWEATHERMAP_API_KEY "your_api_key"
   #define WEATHER_CITY "YourCity"
   #define WEATHER_COUNTRY_CODE "YourCountryCode"
   ```
3. Configure Arduino IDE with the board settings above.
4. Compile and upload.

## Features

- WiFi auto-reconnect with exponential backoff
- NTP time synchronization
- HTTP response validation and JSON parse error handling with retry logic
- Deep sleep between updates for power management
- Partial e-paper refresh support
- Modular code structure

## File Structure

| File | Purpose |
|---|---|
| `Weather_Clock.ino` | Main application logic |
| `config.h` | WiFi, API credentials, and named constants |
| `networking.h` | WiFi connection with retry/backoff |
| `display_utils.h` | Display helper functions |
| `EPD*.h/cpp` | E-Paper display drivers and drawing primitives |
| `spi.h/cpp` | Bit-banged SPI communication |
| `pic.h` | Image/icon bitmap data |
| `EPDfont.h` | ASCII font data |
| `EPD_Init.h` | Display resolution and pin definitions |

## Notes

- Do not commit `config.h` (contains sensitive credentials).
- Reference: [Elecrow Wiki - CrowPanel ESP32 E-Paper 4.2-inch Arduino Tutorial](https://elecrow.com/pub/wiki/CrowPanel_ESP32_E-Paper_4.2-inch_Arduino_Tutorial.html)

</content>