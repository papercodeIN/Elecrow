# ESP32 E-Paper Wireless Text Display

Web-based wireless text display controller for the Elecrow CrowPanel 4.2-inch ESP32 E-Paper HMI Display. Update up to 8 rows of text from any device on your WiFi network with real-time preview.

## Hardware

- Elecrow CrowPanel ESP32-S3 4.2" E-Paper HMI Display
- E-paper panel: GxEPD2_420_GYE042A87 (or compatible 400x300)

## Project Structure

```
esp32-epaper-wireless-text-display/
├── arduino/
│   ├── wireless_text_display.ino
│   └── config.h
├── platformio/
│   ├── src/
│   │   ├── main.ino
│   │   └── config.h
│   ├── data/
│   │   └── index.html
│   └── platformio.ini
├── docs/
│   ├── ArduinoJson.png
│   ├── GxEPD2.png
│   ├── debug.cfg
│   ├── debug_custom.json
│   └── esp32s3.svd
└── README.md
```

| Directory | Purpose |
|-----------|---------|
| `arduino/` | Arduino IDE build. Self-contained, HTML embedded in code, no extra dependencies beyond core libraries. |
| `platformio/` | PlatformIO build. Full features: WiFiManager, OTA, LittleFS-served web UI, REST API. |
| `docs/` | Reference images and debug configuration files. |

## Features

| Feature | Arduino | PlatformIO |
|---------|---------|------------|
| Web interface | Yes (embedded HTML) | Yes (LittleFS) |
| Up to 8 text rows | Yes | Yes |
| Font size selection | Yes | Yes |
| Color inversion | Yes | Yes |
| Border toggle | Yes | Yes |
| Real-time preview | Yes | Yes |
| REST API (`/api/status`, `/api/update`) | Yes | Yes |
| WiFi reconnection | Yes | Yes |
| WiFiManager captive portal | No | Yes |
| OTA firmware updates | No | Yes |
| LittleFS file system | No | Yes |

## Quick Start

### Arduino IDE

1. Install libraries via Library Manager:
   - `GxEPD2`
   - `ArduinoJson` (v6.x)
2. Edit `arduino/config.h` and set `WIFI_SSID` and `WIFI_PASSWORD`
3. Open `arduino/wireless_text_display.ino`
4. Select board: **ESP32S3 Dev Module**
5. Upload

### PlatformIO

1. Install [PlatformIO](https://platformio.org/)
2. Navigate to the `platformio/` directory
3. Build and upload firmware:
   ```bash
   pio run -t upload
   ```
4. Upload LittleFS filesystem:
   ```bash
   pio run -t uploadfs
   ```
5. On first boot, connect to the `EPaper-Display` AP (password: `display123`) to configure WiFi via captive portal

## Configuration

Edit `config.h` in the respective build folder:

| Setting | Default | Description |
|---------|---------|-------------|
| `DEVICE_NAME` | `EPaper-Display` | Hostname / AP SSID |
| `DISPLAY_WIDTH` | `400` | Screen width in pixels |
| `DISPLAY_HEIGHT` | `300` | Screen height in pixels |
| `WEB_SERVER_PORT` | `80` | HTTP server port |
| `MAX_TEXT_LENGTH` | `30` | Max characters per row |
| `FONT_SIZE_SMALL` | `12` | Small font point size |
| `FONT_SIZE_MEDIUM` | `18` | Medium font point size |
| `FONT_SIZE_LARGE` | `24` | Large font point size |
| `PIN_CS` | `45` | SPI chip select |
| `PIN_DC` | `46` | Data/command pin |
| `PIN_RES` | `47` | Reset pin |
| `PIN_BUSY` | `48` | Busy pin |
| `PIN_PWR` | `7` | Display power pin |

## API Reference

### GET /api/status

Returns current display state as JSON.

**Response:**
```json
{
  "rows": [
    {"text": "Hello", "fontSize": 24},
    {"text": "", "fontSize": 18}
  ],
  "border": true,
  "invertColors": false,
  "ip": "192.168.1.100"
}
```

### POST /api/update

Updates the display. Accepts JSON body.

**Request body:**
```json
{
  "rows": [
    {"text": "Line 1", "fontSize": 24},
    {"text": "Line 2", "fontSize": 18},
    {"text": "", "fontSize": 12},
    {"text": "", "fontSize": 12},
    {"text": "", "fontSize": 12},
    {"text": "", "fontSize": 12},
    {"text": "", "fontSize": 12},
    {"text": "", "fontSize": 12}
  ],
  "border": true,
  "invertColors": false
}
```

**Responses:**
- `200` `{"status":"ok"}` — display updated
- `400` `{"error":"Invalid JSON"}` — malformed request
- `400` `{"error":"Invalid settings"}` — validation failed

### cURL example

```bash
curl -X POST http://<device-ip>/api/update \
  -H "Content-Type: application/json" \
  -d '{"rows":[{"text":"Hello","fontSize":24}],"border":true,"invertColors":false}'
```

## Pin Mapping

| Function | GPIO |
|----------|------|
| Display Power | 7 |
| SPI CS | 45 |
| DC | 46 |
| Reset | 47 |
| Busy | 48 |

## License

MIT

</content>