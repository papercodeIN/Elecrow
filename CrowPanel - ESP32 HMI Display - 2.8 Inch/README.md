<h1 align="center">🌟 Elecrow CrowPanel - ESP32 2.8" HMI Display (CYD) 🌟</h1>

---
<p align="center">
  <span style="font-size: 1.1em; color: #FFD700; font-weight: bold;">✨ Enjoying this project? Support our work! ✨</span>
</p>

<p align="center" style="margin: 15px 0;">
  <a href="https://buymeacoffee.com/pylin" target="_blank">
    <img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me a Coffee" style="height: 40px; width: 150px;">
  </a>
</p>

<p align="center" style="margin: 15px 0;">
  <a href="https://www.youtube.com/channel/UCKKhdFV0q8CV5vWUDfiDfTw" target="_blank">
    <img src="https://img.shields.io/badge/SUBSCRIBE%20ON%20YOUTUBE-FF0000?style=for-the-badge&logo=youtube&logoColor=white" alt="Subscribe on YouTube" style="height: 40px;">
  </a>
</p>

---

## 📋 Product Information

| Spec | Detail |
|-----:|:-------|
| **Product** | 2 PACK 2.8inch ESP32 Solo Miner LCD Display |
| **SKU** | DHM05428D |
| **Main Processor** | ESP32-WROOM-32-N4 (Dual-core LX6, 240MHz) |
| **Display** | 2.8" TFT-LCD, 320×240, ILI9341V Driver |
| **Touch** | Resistive Touch Screen |
| **Connectivity** | WiFi 2.4GHz (802.11 b/g/n) + Bluetooth 5.0 |
| **Power** | DC 5V-2A (USB-C) / Battery 3.7-4.2V |
| **Interfaces** | TF Card, I2C, GPIO, UART1, UART0, Speaker |
| **Buttons** | BOOT + Reset |
| **Dimensions** | 57mm × 85.7mm |
| **Weight** | 61g (per unit) |

---

## 🔗 Useful Links

- [📖 Wiki](https://www.elecrow.com/wiki/esp32-display-702727-intelligent-touch-screen-wi-fi26ble-800480-hmi-display.html)
- [🛒 Product Page](https://www.elecrow.com/2-8inch-esp32-miner-lcd-display-2pcs-cryptocurrency-solo-miner-with-1000kh-s-hashrate.html)
- [⭐ Elecrow Referral Link for $5 Coupon ⭐](https://www.elecrow.com/referral-program/Nzk5MjJqMnQ/)

---

## 🔌 Pin Mapping (CYD 2.8")

| Function | GPIO Pin |
|---------:|:---------|
| TFT_MOSI | 13 |
| TFT_SCLK | 14 |
| TFT_CS | 15 |
| TFT_DC | 2 |
| TFT_RST | -1 (not connected) |
| TFT_BL (Backlight) | 27 |
| TOUCH_CS | 33 |

| Connector | Spec |
|----------:|:-----|
| SPK (Speaker) | PH2.0-2P |
| UART1 | HY2.0-4P |
| GPIO_D | HY2.0-4P |
| I2C | HY2.0-4P |

---

## 📁 Projects

### 1. 🕐 Military Clock - 10-OCT-2026

An army-style dual-timezone digital clock with military green aesthetic and custom 7-segment digits.

**Features:**
- Dual timezone display for **IST** and **UKT**
- UK local time uses BST/DST rules automatically
- 12-hour or 24-hour format via `USE_12H_FORMAT`
- Custom 7-segment digits drawn directly with TFT primitives
- Date display (DD MMM YYYY) and day of the week
- Day progress bar (0000–2359) with percentage complete
- NTP time sync over WiFi (auto-updates from internet)
- Status bar showing NTP sync status and 12H/24H mode
- Military green + amber color scheme on black background

**Available in two versions:**

| Version | Path | Notes |
|--------:|:-----|:------|
| **PlatformIO** | `Project/Military_Clock - 10-OCT-2026/platformio/` | Pin config via `build_flags` in `platformio.ini` |
| **Arduino IDE** | `Project/Military_Clock - 10-OCT-2026/arduino/MilitaryClock/` | Requires `User_Setup.h` to be copied into TFT_eSPI library |

---

## 🛠️ Development Environment Setup

### Required Software

| Component | Version | Purpose |
|----------:|:--------|:--------|
| ESP32 Arduino Core | v2.0.x | Board support |
| TFT_eSPI | v2.5.43+ | Display driver library |
| Time | v1.6.1+ | Time functions (PlatformIO only) |

### Option A: PlatformIO (Recommended)

1. Install [PlatformIO](https://platformio.org/) via pip or VS Code extension
2. Open the `platformio/` folder as a project
3. **Configure WiFi credentials** (one-time setup):
   - Copy `include/secrets.example.h` to `include/secrets.h`
   - Edit `include/secrets.h` with your actual WiFi credentials
   - `secrets.h` is gitignored — your credentials stay local
4. Timezones are configured in `src/main.cpp` using POSIX timezone strings:
    ```cpp
    const char* TZ_IST = "IST-5:30";
    const char* TZ_UK  = "GMT0BST,M3.5.0/1,M10.5.0/2";
    ```
5. **Board Configuration** (per [Elecrow Wiki](https://www.elecrow.com/wiki/CrowPanel_ESP32_2.8-inch_with_PlatformIO.html)):
   - The `platformio.ini` uses `board = esp32dev` (ESP32 Dev Module)
   - Elecrow's reference example uses `board = denky32` with a pinned platform
   - Our configuration builds and uploads successfully with `esp32dev`
    - Key `build_flags` match the CYD 2.8" pinout (TFT pins, backlight on GPIO 27, touch CS on GPIO 33)

6. Build and upload:
   ```bash
   pio run -t upload
   ```
7. Monitor serial output:
   ```bash
   pio device monitor
   ```

### Option B: Arduino IDE

1. **Install ESP32 Board Support**
   - Go to `File > Preferences`
   - Add to "Additional Board Manager URLs":
     ```
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
     ```
   - Go to `Tools > Board > Boards Manager`, search **esp32** by Espressif Systems, and install **v3.3.8**

2. **Install TFT_eSPI Library**
   - Go to `Sketch > Include Library > Manage Libraries`
   - Search **TFT_eSPI** by Bodmer, install v2.5.43+

3. **Configure TFT_eSPI Pin Mapping**
   - Copy `User_Setup.h` from the `arduino/MilitaryClock/` folder
   - Replace the file at your Arduino libraries path:
     ```
     C:\Users\<YourUser>\Documents\Arduino\libraries\TFT_eSPI\User_Setup.h
     ```

4. **Configure WiFi credentials** (one-time setup):
   - Copy `arduino/MilitaryClock/secrets.example.h` to `arduino/MilitaryClock/secrets.h`
   - Edit `arduino/MilitaryClock/secrets.h` with your actual WiFi credentials
   - `secrets.h` is gitignored — your credentials stay local

5. **Board Settings in Arduino IDE** (per [Elecrow Wiki](https://www.elecrow.com/wiki/ESP32-Display-28-inch-HMI-Arduino-Tutorial.html))

   | Setting | Value |
   |--------:|:------|
   | Board | **ESP32-WROOM-DA Module** |
   | Upload Speed | Default (921600) |
   | Flash Frequency | Default (80MHz) |
   | Flash Size | Default (4MB / 32Mb) |
   | Partition Scheme | Default (Default 4MB with spiffs) |
   | Port | (Select your COM port) |

   > **Important**: Keep all other Tools menu options at their **default values**. Selecting **ESP32-WROOM-DA Module** is critical — "ESP32 Dev Module" will not work correctly with this board.

6. **Upload Procedure**
   - Click **Upload** and wait for write progress
   - If it stays at `Connecting...` for a long time: **hold the BOOT button** on the board, release once writing begins
   - If still fails, re-check the COM port selection

7. Timezones are already configured in `MilitaryClock.ino` for IST and UKT
8. Open **Serial Monitor** and set baud rate to **115200** to verify

---

## ⏰ Timezone Configuration

The clock syncs UTC from NTP, then converts it locally using POSIX timezone strings.

| Display | POSIX TZ String | Notes |
|--------:|:----------------|:------|
| IST | `IST-5:30` | India Standard Time, UTC+5:30 |
| UKT | `GMT0BST,M3.5.0/1,M10.5.0/2` | UK local time with BST/DST rules |

Set the display mode with:
```cpp
#define USE_12H_FORMAT  1  // 1 = 12H, 0 = 24H
```

---

## 📂 Folder Structure

```
CrowPanel - ESP32 HMI Display - 2.8 Inch/
├── README.md
└── Project/
    └── Military_Clock - 10-OCT-2026/
        ├── platformio/
        │   ├── platformio.ini
        │   ├── src/
        │   │   └── main.cpp
        │   ├── include/
        │   │   └── secrets.example.h   (copy to secrets.h, edit credentials)
        │   ├── lib/
        │   └── test/
        └── arduino/
            └── MilitaryClock/
                ├── MilitaryClock.ino
                ├── User_Setup.h
                └── secrets.example.h   (copy to secrets.h, edit credentials)
```

---

## 🔧 Troubleshooting

| Issue | Solution |
|------:|:---------|
| Blank screen | Verify pin mapping matches your board version. Check `TFT_BL` pin 27 is set. |
| No WiFi connection | Double-check SSID/password. The board only supports **2.4GHz** networks. |
| Wrong time displayed | Check NTP sync in Serial Monitor and verify the POSIX timezone strings. |
| Upload fails | Hold **BOOT** button while uploading. Try lower upload speed (115200). |
| TFT_eSPI compile errors (Arduino) | Make sure you replaced `User_Setup.h` in the TFT_eSPI library folder. |
| Touch not working | Touch is not used by the Military Clock UI. `TOUCH_CS = 33` is kept for board config only. |

---

<h1 align="center">📢 Reach Out to Us for Any Queries ⤵️</h1>

<table align="center">
  <thead>
    <tr>
      <th>📞 Contact Method</th>
      <th>🔗 Details</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>Call / WhatsApp</td>
      <td><a href="https://wa.me/919974477759">+91-9974477759</a></td>
    </tr>
    <tr>
      <td>Telegram</td>
      <td><a href="https://t.me/fusionautomate">@fusionautomate</a></td>
    </tr>
    <tr>
      <td>Email</td>
      <td><a href="mailto:eng.innovativ@gmail.com">eng.innovativ@gmail.com</a></td>
    </tr>
  </tbody>
</table>

---
