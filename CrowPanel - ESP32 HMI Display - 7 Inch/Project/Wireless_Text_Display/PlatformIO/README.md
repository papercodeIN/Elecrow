# Wireless Text Display — CrowPanel 7" ESP32-S3 HMI V3.0

Remote-controlled 5-line text display for the **Elecrow CrowPanel 7" ESP32-S3 HMI (V3.0)**.
The panel shows up to 5 customizable text lines (text, color, size) over an LVGL
"tech HUD" interface; all content is configured from any browser over WiFi.

## Hardware

| Component   | Detail                                                       |
|-------------|--------------------------------------------------------------|
| MCU         | ESP32-S3-WROOM-1-N4R8 (4 MB flash, 8 MB OPI PSRAM)           |
| Display     | 800x480 RGB LCD, 16-bit RGB565, driven by LovyanGFX          |
| Touch       | GT911 capacitive controller, reset routed via PCA9557 expander |
| Backlight   | GPIO 2 (PWM, 300 Hz)                                         |

## Project layout

```
PlatformIO/
├── platformio.ini          # env: crowpanel_7 (espressif32@6.9.0, arduino)
├── boards/                 # custom board def: esp32-s3-devkitc1-n4r8
├── include/
│   ├── lv_conf.h           # LVGL 8.3 config (RGB565, Montserrat fonts 12–48)
│   ├── webui.h             # web UI HTML (PROGMEM) + reset section
│   ├── touch.h             # GT911 touch glue (FT6X36/XPT2046 variants kept)
│   └── secrets.h           # optional hardcoded WiFi credentials fallback
├── lib/
│   ├── PCA9557/            # I2C IO expander (LCD/touch reset on V3.0 panel)
│   └── TAMC_GT911/         # GT911 touch driver
└── src/
    ├── main.cpp            # display, WiFi, web server, AP fallback
    ├── ui*.c/h             # LVGL HUD screen (5 channels + status bar)
    └── ui_events.c
```

## Build & flash

```bash
cd PlatformIO
pio run                      # build
pio run -t upload --upload-port COM4   # flash (adjust port)
pio device monitor           # serial log @ 115200
```

## First boot & WiFi

1. **Credentials saved** (NVS namespace `crowpanel_v3`, or fallback `include/secrets.h`):
   the device joins your network. The on-screen status bar shows the assigned IP;
   open `http://<device-ip>/` from any browser.
2. **No credentials / connection failure**: the device starts an access point
   **`CrowPanel_Display`** (password `12345678`). Connect and open
   `http://192.168.4.1/` — the portal address shown in the UI is always dynamic.
   All unknown hostnames are answered by a DNS wildcard (captive portal), so on
   most phones the configuration page opens automatically.
3. Enter SSID/password in *Network Configuration* → the device saves to NVS and
   reboots into station mode.
4. *Credential Purge* (visible when connected in station mode) erases saved
   credentials and reboots into AP mode.

## Web API

| Route                 | Method | Description                                                        |
|-----------------------|--------|--------------------------------------------------------------------|
| `/`                   | GET    | Web UI (HUD-themed config page)                                    |
| `/update`             | POST   | Form fields `bgcolor`, `text0..4`, `color0..4`, `size0..4` (18/24/36/48) |
| `/api/wifi`           | POST   | Fields `ssid`, `pass` — saves credentials and reboots              |
| `/api/wifi/reset`     | POST   | Clears saved credentials and reboots into AP mode                  |

Any other path redirects to `/` (captive portal behavior).

## Notes

- The 5 channels map to `ui_TextLabel[0..4]` on screen; sizes 18/24/36 map to
  Montserrat 18/24/36 and everything larger uses Montserrat 48.
- Text payloads, colors and sizes are HTML-escaped before being echoed back
  into the configuration page.
- `secrets.h` is optional and git-ignored so real credentials never get
  committed. To hardcode credentials, copy `include/secrets.h.example` to
  `include/secrets.h` and fill in your SSID/password; without it the device
  relies on credentials saved via the web interface, or starts in AP mode.
  A template copy is tracked as `secrets.h.example`.
