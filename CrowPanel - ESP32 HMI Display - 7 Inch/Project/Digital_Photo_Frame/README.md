# CrowPanel 7-inch ESP32-S3 Digital Photo Frame

An ESP32-S3 photo frame for the Elecrow CrowPanel 7-inch 800×480 RGB display. The PlatformIO application provides a web dashboard, SD-card photo slideshow, image transitions, remote playback controls, and Wi-Fi setup portal.

## Hardware and support status

- Target: CrowPanel 7-inch ESP32-S3 RGB panel, 800×480, 8 MB OPI PSRAM.
- Display driver: LovyanGFX RGB bus configuration in `PlatformIO/include/display_7inch.h`.
- Storage: SD-over-SPI pins currently configured as MOSI 11, MISO 13, SCK 12, CS 10 in `PlatformIO/include/config.h`. **Verify this mapping against your board revision and SD-slot schematic before testing.**
- Touch input is not currently implemented.
- Only the PlatformIO project is supported. Legacy Arduino/demo directories are not part of the build.

## Build and flash in VS Code

1. Open `Project/Digital_Photo_Frame/PlatformIO` as the PlatformIO project (or open the parent project and select that directory).
2. Install the PlatformIO IDE extension if needed.
3. Optionally copy `include/secrets.h.example` to `include/secrets.h` and set `DEFAULT_SSID` and `DEFAULT_PASS`. If omitted, setup uses the `PhotoFrame-Setup` access point.
4. Build the default `esp32s3_7inch` environment using **PlatformIO: Build** or `pio run`.
5. Connect the board over USB and upload using **PlatformIO: Upload** or `pio run -t upload`.
6. Open the serial monitor at 115200 baud.

The target configuration is in `PlatformIO/platformio.ini`; it selects the local N4R8 ESP32-S3 board definition, LovyanGFX, PSRAM, and the asynchronous web-server libraries. No `esp32dev` environment is present.

## Application use

Insert a FAT32 microSD card and create `/photos` (if the firmware does not create it automatically). On first boot without saved credentials, connect to `PhotoFrame-Setup`, then visit `http://192.168.4.1`. Once connected to Wi-Fi, use the device's IP address to upload JPEG, PNG, or BMP files and control the slideshow.

## Important limitations

This port has not yet been compiled or tested on the physical 7-inch panel in this environment because PlatformIO CLI is unavailable here. Verify the display timing, SD SPI wiring, image decoding, framebuffer/PSRAM use, and brightness behavior on your board. The README intentionally avoids claiming these are verified. The dashboard source copies in `www/` and `data/` are static assets; the firmware currently embeds its UI from `PlatformIO/include/index_html.h`.
