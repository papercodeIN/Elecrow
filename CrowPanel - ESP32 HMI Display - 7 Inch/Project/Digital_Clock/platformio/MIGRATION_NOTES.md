# Migration Notes - CrowPanel 7" Digital Clock (PlatformIO)

## Source
- Arduino IDE project: `clock/` (clock.ino + UI/ SquareLine LVGL sources)
- Reference PlatformIO project: `../2.8 Inch/.../platformio`

## What was done
- Created standalone PlatformIO project under `platformio/`.
- `platformio.ini` targets ESP32-S3 with LovyanGFX 1.1.12, LVGL 8.3.11, TAMC_GT911.
- Moved UI sources (`ui*.c`) to `src/` and headers to `include/`.
- Added `lv_conf.h` (color depth 16, required fonts, default theme).
- Converted `clock.ino` to `src/main.ino` (PlatformIO compiles .ino with Arduino preprocessor).
- Removed unused `PCA9557` include; kept GT911 touch (touch.h).
- Declared interactive functions as `extern "C"` to match SquareLine-generated `ui_events.c` C linkage.
- Resolved duplicate `updateStatusMessage` (kept in `ui_events.c`).

## Hardware mapping
- RGB panel pins per `clock.ino` LovyanGFX LGFX class (800x480).
- Backlight PWM on GPIO 2.
- Touch: GT911 on SDA 19 / SCL 20.
- I2C Wire.begin(19,20).

## Build
```
cd platformio
pio run
```
Result: SUCCESS (firmware.bin generated).

## Validation
1. `pio run` completes with [SUCCESS].
2. Flash with `pio run -t upload`.
3. Serial monitor 115200: shows init + WiFi/NTP status.
4. Display shows clock UI; touch responds.

## WiFi credentials
- Compile-time: edit `include/secrets.h` (WIFI_SSID / WIFI_PASSWORD). Leave empty to use AP mode.
- Runtime: if no credentials are provided, device starts AP "CrowPanel-Config"; connect and open 192.168.4.1 to configure. Credentials are saved to NVS.