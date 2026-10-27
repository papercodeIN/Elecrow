# Wireless Text Display — CrowPanel 7" ESP32-S3 HMI (V3.0)

Remote-controlled 5-line text display for the **Elecrow CrowPanel 7" ESP32-S3 HMI (V3.0)**.
The panel shows up to 5 fully customizable text lines over an LVGL interface matching
the web UI's dark tech theme; everything (text, colors, sizes, background) is configured
from any browser over WiFi.

## Photos

> **TODO: add photos** — drop a photo of the display at
> `docs/photos/display_screen.jpg` and a screenshot of the web page at
> `docs/photos/web_interface.png` (keep the file names), then they appear here automatically.

| Display screen | Web interface |
|----------------|---------------|
| ![Display screen photo placeholder](docs/photos/display_screen.jpg) | ![Web interface screenshot placeholder](docs/photos/web_interface.png) |

## Features

- 5 text channels — each with its own text, color, and size (SMALL / MEDIUM / LARGE / XLARGE)
- Configurable background color
- Clean, self-contained web UI (no external fonts/CDN — renders identically offline in AP mode)
- On-screen IP address so you always know where to connect
- Dedicated AP info screen when WiFi is not configured, with SSID / password / URL
- WiFi credentials saved in NVS, with optional `secrets.h` fallback and captive-portal AP setup
- Credential purge from the web page to re-enter AP mode
- Flicker-free: the screen only redraws when content actually changes

## Hardware

| Component   | Detail                                                          |
|-------------|-----------------------------------------------------------------|
| MCU         | ESP32-S3-WROOM-1-N4R8 (4 MB flash, 8 MB OPI PSRAM)              |
| Display     | 800x480 RGB LCD, 16-bit RGB565, driven by LovyanGFX             |
| Touch       | GT911 capacitive controller, reset routed via PCA9557 expander  |
| Backlight   | GPIO 2 (PWM, 300 Hz)                                            |

## Project layout

```
Wireless_Text_Display/
├── README.md               <- this file
├── PlatformIO/             <- the firmware project (open THIS folder in VS Code)
│   ├── platformio.ini      # env: crowpanel_7 (espressif32@6.9.0, arduino)
│   ├── boards/             # custom board def: esp32-s3-devkitc1-n4r8
│   ├── include/            # lv_conf.h, webui.h, touch.h, secrets template
│   ├── lib/                # vendored PCA9557 + patched TAMC_GT911
│   └── src/                # main.cpp + LVGL screens
└── docs/photos/            # place README photos here
```

## Getting started with VS Code (PlatformIO IDE)

1. **Install [Visual Studio Code](https://code.visualstudio.com/)** and open it.
2. Install the **PlatformIO IDE** extension (`platformio.platformio-ide`) from the
   Extensions marketplace (`Ctrl+Shift+X`). Let it finish installing its core
   packages (bottom-right progress).
3. **File → Open Folder…** and select the `PlatformIO` folder
   (`…/Project/Wireless_Text_Display/PlatformIO`) — open that folder itself, not the
   repo root, so PlatformIO picks up `platformio.ini`.
4. Wait for PlatformIO to download the toolchain and libraries (first time only;
   the project pins espressif32 6.9.0, LVGL 8.3.11 and LovyanGFX 1.1.12).
5. **Build:** click the **✓ (Build)** button in the bottom toolbar, or the
   PlatformIO alien icon → *Project Tasks → crowpanel_7 → General → Build*.
6. **Connect the display** via USB and **Upload:** click the **→ (Upload)** button.
   - If several serial ports exist, pick the CrowPanel's port when prompted
     (Windows: `COM4`, `COM11`, …; Linux: `/dev/ttyUSB0` / `/dev/ttyACM0`), or run
     *Project Tasks → General → Upload* with `upload_port` set in `platformio.ini`.
7. **Serial monitor:** click the **plug icon (Serial Monitor)** — 115200 baud — to
   watch the boot log, WiFi connection and assigned IP.

### CLI alternative

```bash
cd PlatformIO
pio run                                # build
pio run -t upload --upload-port COM4   # flash (adjust port)
pio device monitor                     # serial log @ 115200
```

## First boot & WiFi

1. **Credentials saved** (from a previous run in NVS, or an optional
   `include/secrets.h` — copy `include/secrets.h.example` and fill it in; it is
   git-ignored so real credentials are never committed): the device joins your
   network and shows its **IP on the bottom bar of the screen**. Open
   `http://<device-ip>/` in any browser.
2. **No credentials / connection failure:** the device starts an access point
   **`CrowPanel_Display`** (password `12345678`) and switches to a full-screen AP
   info panel showing the SSID, password and URL. Connect with a phone — the
   captive portal usually opens the page automatically, otherwise browse to
   `http://192.168.4.1/`.
3. Enter SSID/password in *WIFI SETUP* → the device saves to NVS and reboots into
   station mode.
4. *CREDENTIAL PURGE* (visible when connected) erases saved credentials and
   reboots into AP mode.

## Web API

| Route             | Method | Description                                                                 |
|-------------------|--------|-----------------------------------------------------------------------------|
| `/`               | GET    | Web UI (config page)                                                        |
| `/update`         | POST   | Form fields `bgcolor`, `text0..4`, `color0..4`, `size0..4` (18/24/36/48)    |
| `/api/wifi`       | POST   | Fields `ssid`, `pass` — saves credentials and reboots                       |
| `/api/wifi/reset` | POST   | Clears saved credentials and reboots into AP mode                           |

Any other path redirects to `/` (captive portal behavior — this is also what makes
the setup page pop up automatically on phones).

## Notes

- Text payloads, colors and sizes are HTML-escaped before being echoed back into
  the configuration page.
- The 5 channels map to `ui_TextLabel[0..4]` on screen; sizes 18/24/36 use
  Montserrat 18/24/36 and XLARGE uses Montserrat 48.
- `lib/TAMC_GT911` is a vendored, patched copy (the reset pin operations are
  skipped when no direct GPIO is wired — on the V3.0 panel the GT911 reset is
  routed through the PCA9557 I2C expander instead).
