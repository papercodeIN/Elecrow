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
| TFT_BL (Backlight) | 21 |
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

An army-style digital clock with military green aesthetic, custom 7-segment digits, and CRT scanline effects.

**Features:**
- 24-hour (military) time format with large custom 7-segment display
- Blinking colon separator between hours and minutes
- Seconds display in amber color
- Date display (DD MMM YYYY) and day of the week
- Day progress bar (0000–2359) with percentage complete
- NTP time sync over WiFi (auto-updates from internet)
- CRT/military terminal scanline overlay effect
- Status bar showing system state and NTP sync status
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
4. Set your timezone (seconds offset from UTC) in `src/main.cpp`:
   ```cpp
   const long GMT_OFFSET = -18000;  // EST = UTC-5 = -5*3600
   const int  DST_OFFSET = 3600;   // 1 hour DST offset
   ```
5. Build and upload:
   ```bash
   pio run -t upload
   ```
6. Monitor serial output:
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
   - Go to `Tools > Board > Boards Manager`, search **esp32** by Espressif, and install

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

5. **Board Settings in Arduino IDE**

   | Setting | Value |
   |--------:|:------|
   | Board | ESP32 Dev Module |
   | Upload Speed | 921600 |
   | Flash Frequency | 80MHz |
   | Flash Size | 4MB |
   | Partition Scheme | Default 4MB with spiffs |
   | Port | (Select your COM port) |

6. **Set your timezone** in `MilitaryClock.ino` (line 9)
7. Click **Upload**

---

## ⏰ Common Timezone Offsets

| Timezone | GMT_OFFSET (seconds) |
|---------:|:---------------------|
| UTC | 0 |
| EST (UTC-5) | -18000 |
| CST (UTC-6) | -21600 |
| MST (UTC-7) | -25200 |
| PST (UTC-8) | -28800 |
| IST (UTC+5:30) | 19800 |
| CET (UTC+1) | 3600 |
| JST (UTC+9) | 32400 |
| AEST (UTC+10) | 36000 |

> Set `DST_OFFSET = 3600` if your region observes daylight saving time, otherwise set it to `0`.

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
| Blank screen | Verify pin mapping matches your board version. Check `TFT_BL` pin 21 is set. |
| No WiFi connection | Double-check SSID/password. The board only supports **2.4GHz** networks. |
| Wrong time displayed | Adjust `GMT_OFFSET` for your timezone. See timezone table above. |
| Upload fails | Hold **BOOT** button while uploading. Try lower upload speed (115200). |
| TFT_eSPI compile errors (Arduino) | Make sure you replaced `User_Setup.h` in the TFT_eSPI library folder. |
| Touch not working | Ensure `TOUCH_CS = 33` is set. Use the resistive pen included in the box. |

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
