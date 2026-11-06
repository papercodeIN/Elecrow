# Android Auto–Style Turn-by-Turn Navigation Display

> **Status: FIRMWARE BUILT — bench/hardware testing pending** (M1–M2 code
> complete, compiles clean; you flash and report serial output; M3+ in the
> milestone table below). This document is the complete build documentation:
> architecture, protocol, setup steps, companion app links, verification
> status, disadvantages/limitations and troubleshooting.

A BLE turn-by-turn navigation HUD for the Elecrow CrowPanel 2.8" ESP32 (CYD)
board. Your phone runs **Google Maps** as usual; a companion app (or Tasker
profile) reads the Maps navigation state and streams structured instructions
over **Bluetooth Low Energy** to the display. The 2.8" screen shows the next
maneuver as a large arrow, the distance counting down, the street name, and an
**instant "TURN NOW" prompt** — flashing panel plus a speaker chime — when the
turn is imminent.

> **What this is not:** this is *not* an implementation of the Android Auto
> wire protocol (see [Why Not Real Android Auto?](#-why-not-real-android-auto)).
> The phone stays in full control of navigation; the display is a dedicated
> secondary screen. The approach is proven by several open-source projects and
> is the only realistic path on ESP32-WROOM hardware.

---

## 📐 Architecture

```
┌──────────────────┐        ┌───────────────────────────┐        ┌─────────────────────────────┐
│  Android phone   │        │  Companion (on the phone) │        │  CrowPanel 2.8" ESP32       │
│                  │        │                           │        │                             │
│  Google Maps ────┼──────▶ │  Tasker + AutoNotification│        │  BLE GATT server (NUS)      │
│  nav notification│ notif. │  …or… Kotlin companion app│  BLE   │  JSON → nav state machine   │
│  GPS speed ──────┼──────▶ │  (NotificationListener +  │ ─────▶ │  TFT_eSPI card UI (ILI9341) │
│                  │        │   BLE GATT client)        │  JSON  │  Speaker chimes (GPIO 26)   │
└──────────────────┘        └───────────────────────────┘        └─────────────────────────────┘
```

- **Phone**: runs Google Maps normally (online, cellular data). Nothing about
  Maps is modified.
- **Companion**: extracts maneuver type, distance to turn, street name, "then"
  street, and state; sends a small JSON update over BLE on change (≤ 1 Hz) plus
  a 5 s heartbeat ping.
- **Display**: pure receiver. Renders the nav card and runs the urgency logic
  locally (amber highlight → flashing red + chimes) so prompts fire even
  between phone updates.

**Why BLE and not Wi-Fi:** in the car the phone's Wi-Fi must stay free (mobile
data / hotspot duties). BLE coexists with cellular data, reconnects
automatically, and draws almost no power. All prior-art projects converged on
the same choice.

---

## 🏁 Project Status — Milestone Checklist

| # | Milestone | Status |
|---|---|---|
| M0 | Plan documented (this README) | ✅ Done |
| M1 | Firmware scaffold: LCD bring-up, nav card UI, vector arrows, themes, gestures | ✅ Code complete — **hardware bring-up pending your flash** |
| M2 | BLE link: NUS GATT server, bonding policy, JSON parser, reconnect, test feed | ✅ Code complete — **bench test pending** |
| M3 | Tasker MVP end-to-end with real Google Maps | ⬜ `docs/tasker-setup.md` written; needs a drive test |
| M3b | Drive Assistant Zero (DEMP1993) companion app support | ⬜ APK available from their releases; **firmware needs a token-mapping patch** in `nav_state.cpp` (details in [References](#-references)) |
| M4 | Kotlin companion app | ⬜ Source complete in `android/` — **not yet built** (needs Android Studio; see [Verification Status](#-verification-status-honest-numbers)) |
| M5 | UX polish on the road (chime tuning, thresholds, night use) | ⬜ |
| M6 | Vehicle install: mount, USB-C power, photos | ⬜ |

---

## 🚀 Quick Start — Flash the Display

1. Install [PlatformIO](https://platformio.org/) (VS Code extension or pip).
2. Open this folder in VS Code: `Project/Android_Auto_Nav_Display/platformio/`
   (the folder containing `platformio.ini`).
3. Connect the display by USB-C to the PC.
4. Build + upload (replace `COM4` with your port):

   ```powershell
   python -m platformio run -e crowpanel_28                # build only
   python -m platformio run -e crowpanel_28 -t upload --upload-port COM4
   python -m platformio device monitor                     # serial, 115200
   ```

5. **Expected serial output on a good boot:**

   ```
   === AA Nav Display ===
   [ui] init ok (sprite on)
   [ble] advertising as "AA Nav Display" (6E400001-...)
   [main] ready - waiting for phone
   ```

   The screen shows *AA Nav Display — Waiting for phone... Pair in Bluetooth
   settings*.

6. **Pair:** Android Settings → Bluetooth → tap **AA Nav Display** → pair
   (Just Works, no passkey). It re-advertises after every boot and
   auto-reconnects afterwards. This is the same bonding recipe proven on the
   [Bluetooth Stream Deck](../../../CrowPanel%20-%20ESP32%20HMI%20Display%20-%207%20Inch/Project/Bluetooth_Stream_Deck/)
   project: factory Bluetooth address, Just Works, bonds are never erased.
7. If pairing misbehaves after re-flashing older/experimental builds, remove
   **every** "AA Nav Display" entry in the phone's Bluetooth list, reboot the
   display, and pair once (see Troubleshooting).

**No hardware was flashed or connected during development** — everything above
is run by you, manually.

---

## 🧪 Test From a PC (before involving a phone)

`tools/test_ble_sender.py` is a scripted "fake phone" that pushes realistic
nav JSON over BLE. The display reacts exactly as it would to real navigation
(arrows, countdown, urgency flash, chimes).

```powershell
pip install bleak
python tools/test_ble_sender.py --scenario approach   # one turn approach
python tools/test_ble_sender.py --scenario reroute    # reroute banner
python tools/test_ble_sender.py --scenario arrive     # arrival
python tools/test_ble_sender.py --scenario loop       # endless demo loop
```

What you should see: the arrow panel changes glyph per leg, the distance
counts down, the panel turns **amber under 150 m**, flashes **red with chimes
under 40 m**, the progress bar advances, and `SIGNAL LOST` appears if you stop
the script mid-navigation for > 10 s. The serial monitor logs every parsed
message: `[nav] m=TR d=320 ds='320 m' ...`.

---

## 📱 Phone Side

### Option A — Tasker, no code (works today)

Full step-by-step guide with the exact Tasker actions, JavaScriptlet parser
and BLE-writer settings: **[docs/tasker-setup.md](docs/tasker-setup.md)**.
Needs Tasker + AutoNotification + [Basic BLE Writer](https://github.com/swankdave/Basic-Ble-Writer)
plugin. Latency ~1 s; fine for proving the system on the road.

### Option B — Our companion app (long-term answer)

Source lives in [`android/`](android/) (Kotlin, Android Studio project,
minSdk 26 / target 34). It reads the Google Maps navigation notification
(`NotificationListenerService`), parses maneuver/distance/street, and streams
protocol v1 JSON over BLE with auto-reconnect and a foreground service.

**How to get an APK today:** there is **no prebuilt APK yet** — build it
yourself (5 minutes, free):

1. Install [Android Studio](https://developer.android.com/studio) (includes
   the SDK + a compatible Gradle/JDK).
2. **File → Open** → select this project's `android/` folder. If Studio asks
   which Gradle distribution to use (the project ships no wrapper), pick
   **Studio's recommended Gradle** and let it sync.
3. **Build → Build App Bundle(s)/APK(s) → Build APK(s)** → install the APK on
   the phone.
4. In the app: **Grant notification access** → **Start navigation link** →
   start navigation in Google Maps.

> Origin & credit: this architecture — and the companion app's
> notification-listener approach — is adapted from the open-source
> **CatDrive** project ([maisonsmd/esp32-google-maps](https://github.com/maisonsmd/esp32-google-maps),
> MIT license, [project write-up](https://maisonsmd.dev/blog/google-maps-on-esp32)),
> which itself ships an Android app + ESP32 firmware for exactly this
> Maps→BLE→display idea. **Note:** CatDrive's app speaks *its own* BLE
> protocol (raw text + bitmap icons) — it does **not** drive this firmware
> directly; our `android/` app is the compatible one. We replaced CatDrive's
> bitmap icon transfer with maneuver codes + vector arrows (less BLE traffic,
> crisper UI) and added the urgency/chime layer.

Planned: signed release APK attached to GitHub Releases so non-builders can
just download (milestone M4 close-out).

---

## 🔍 Research: Prior Art (verified November 2026)

| Project | What it does | What we took from it |
|---|---|---|
| [maisonsmd/esp32-google-maps](https://github.com/maisonsmd/esp32-google-maps) ("CatDrive", MIT) + [blog](https://maisonsmd.dev/blog/google-maps-on-esp32) | Android `NotificationListenerService` parses the Google Maps nav notification; JSON over BLE to an ESP32 + LVGL display; app source in-repo. | **Origin project.** Notification-parsing approach, app adaptation base. Blog confirms the #1 risk: notification structure varies across Maps versions/locales. |
| [alexanderlavrushko/BLE-HUD-navigation-ESP32](https://github.com/alexanderlavrushko/BLE-HUD-navigation-ESP32) (180★) | BLE HUD driven by Sygic's BLE HUD feature (iOS); compact binary protocol (type + speed limit + maneuver code + ASCII distance). | Protocol shape; re-request-if-silent pattern. |
| [DEMP1993/Drive-assistant-device-firmware](https://github.com/DEMP1993/Drive-assistant-device-firmware) | ESP32-C3 round display mirroring Google Maps TBT over BLE. | Same architecture on other ESP32 variants. |
| [jstiefel/esp32_komoot_ble](https://github.com/jstiefel/esp32_komoot_ble) | TBT device via Komoot's BLE API. | Alternative data source if Maps parsing ever breaks. |
| [nisargjhaveri/WirelessAndroidAutoDongle](https://github.com/nisargjhaveri/WirelessAndroidAutoDongle) (ESP32-S3) | Bridges phone ↔ *wired* car head unit for wireless AA. | Shows what true-AA hardware needs; it renders nothing itself. |
| [payalneg/esp32p4-android-auto](https://github.com/payalneg/esp32p4-android-auto) (ESP32-P4) | AA-class head unit on the P4 (400 MHz RISC-V, USB HS, 768 KB SRAM). | Confirms our ESP32-WROOM cannot run a real AA head unit. |

### Why Not Real Android Auto?

1. **No TBT data channel.** AA carries all navigation visuals *inside* an
   H.264 video stream decoded by the head unit. There is no "arrows-only"
   mode.
2. **Decode power.** ESP32-WROOM (240 MHz LX6, ~520 KB RAM, no PSRAM, SPI
   TFT) cannot decode H.264 at usable rates; projects that manage it use
   ESP32-P4/S3-class silicon or a Pi.
3. **Protocol burden.** Proprietary/reverse-engineered: USB accessory mode
   (or BLE + Wi-Fi-Direct), handshake, audio, touch back-channel.
4. **The goal doesn't need it.** Turn prompts are fully served by the
   notification channel while Maps keeps doing all the work.

---

## 🔧 Hardware Reference (CrowPanel 2.8" CYD)

| Spec | Detail |
|---|---|
| **Board** | ESP32-WROOM-32-N4 (dual-core LX6 @ 240 MHz, 4 MB flash, no PSRAM) |
| **Display** | 2.8" TFT-LCD, 240×320, **ILI9341V**, SPI |
| **Touch** | Resistive (XPT2046-class), shared SPI |
| **Radio** | Wi-Fi 2.4 GHz (unused here) + Bluetooth 5.0 (BR/EDR + BLE) |
| **Speaker** | PH2.0-2P connector, driven via LEDC PWM on **GPIO 26** (turn chimes) |
| **Power** | USB-C 5 V (car USB adapter) — also battery 3.7–4.2 V per board spec |
| **Extras** | TF card, I2C, GPIO, UART1 connectors (reserved) |

### Pin mapping (verified against the `Digital_Clock` project config)

| Function | GPIO |
|---|---|
| TFT_MOSI / TFT_SCLK / TFT_MISO | 13 / 14 / 12 |
| TFT_CS / TFT_DC / TFT_RST | 15 / 2 / −1 (not connected) |
| TFT_BL (backlight, PWM-dimmed) | 27 |
| TOUCH_CS | 33 |
| SPEAKER (PWM chimes) | 26 |

Orientation: native portrait 240×320; the firmware runs `setRotation(1)` →
**landscape 320×240**.

### Development environment (PlatformIO, repo convention)

Matches the `Digital_Clock` project: `espressif32@5.3.0`, `board = esp32dev`,
TFT_eSPI 2.5.0 + ArduinoJson 6.21.5, all pin config via `build_flags`
(no `User_Setup.h` copying — see `platformio/platformio.ini` for the exact
flags, which are the single source of truth).

Build result on the reference toolchain: **RAM 12.5 % (41 KB / 320 KB),
flash 89.6 % (1.17 MB of the 1.31 MB default app slot)** — fits without
partition changes; if future features push past ~95 %, switch to
`board_build.partitions = huge_app.csv` (single ~3 MB app slot, no OTA).

**Config cross-check (audited):** the TFT pin block is byte-identical to all
three sibling projects on this board (`Digital_Clock`, `ESP32_NAS`,
`Digital_Photo_Frame`: MISO 12, MOSI 13, SCLK 14, CS 15, DC 2, RST −1, BL 27,
TOUCH_CS 33). The platform version (`espressif32@5.3.0`) and 16 MHz SPI match
`Digital_Clock` exactly; the two 6.5.0 projects additionally define
`USE_HSPI_PORT` (routes the bus to the hardware HSPI peripheral instead of
GPIO-matrix routing) — an optimization that `Digital_Clock` demonstrably does
not need on this platform version, so this project matches the proven 5.3.0
configuration.

---

## 📡 BLE Protocol v1

**Service:** Nordic UART Service (NUS) — works with nRF Connect, Web BLE,
bleak, Tasker BLE plugins and every serial-over-BLE helper.

| Item | UUID |
|---|---|
| Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| RX (phone → display, Write/WriteNR) | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` |
| TX (display → phone, Notify) | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` |

**Framing:** one JSON object per UTF-8 line, terminated `\n`. Keep messages
< ~180 bytes (fits one write at default MTU; the phone chunks at MTU−3 if
longer — the display reassembles fragments until `\n`). MTU 185 is requested
on both sides. JSON (not binary) so the link can be driven live from nRF
Connect.

### Messages

Navigation update (on change, throttled ≤ 1 Hz):

```json
{"t":"nav","m":"TR","d":320,"ds":"320 m","st":"Main St","th":"NH 48","eta":"14:32","rt":"12.4 km","v":0,"s":"nav"}
```

| Field | Type | Meaning |
|---|---|---|
| `t` | str | Message type: `nav`, `state`, `ping` |
| `m` | str | Maneuver code (table below) |
| `d` | int | Meters to maneuver (drives urgency); may be omitted if unknown |
| `ds` | str | Pre-formatted distance exactly as Maps shows it ("320 m", "0.2 mi") — displayed verbatim so unit/locale math never breaks |
| `st` | str | Street/instruction name for this maneuver |
| `th` | str | "Then" street (optional) |
| `eta` | str/null | ETA text (optional) |
| `rt` | str/null | Remaining route distance text (optional) |
| `v` | int | Vehicle speed km/h (0 = unavailable) |
| `s` | str | Nav state: `nav`, `reroute`, `arrived`, `idle` |

State change: `{"t":"state","s":"reroute"}` · Heartbeat: `{"t":"ping"}` every
5 s (display shows *SIGNAL LOST* after 10 s of silence while navigating).
Display → phone: `{"t":"ack"}` after every accepted nav/state message.

### Maneuver codes

| Code | Maneuver | Code | Maneuver |
|---|---|---|---|
| `ST` | Continue straight | `UT` | U-turn |
| `TL` / `TR` | Turn left / right | `MEL` / `MER` | Merge left / right |
| `SL` / `SR` | Slight left / right | `FKL` / `FKR` | Fork left / right |
| `SHL` / `SHR` | Sharp left / right | `RAB` | Roundabout |
| `EXL` / `EXR` | Exit left / right | `ARR` | Arrive at destination |

Glyph notes: `MEL/MER` render as slight-turn arrows, `EXL/EXR` as turn arrows
(v1 simplification); unknown codes draw "?" so a protocol extension can never
blank the screen.

---

## 🖥️ UI (320×240 landscape)

```
┌──────────────────────────────────────────────────────┐
│ ● AA Nav          Recalculating...          42 km/h  │  ← status bar (BLE dot, phase, speed)
├───────────────────┬──────────────────────────────────┤
│                   │              320 m               │  ← distance (verbatim from phone)
│    [ arrow ]      │  Turn right onto Main St         │  ← street (auto-wrap)
│    glyph          │  Then: NH 48                     │  ← "then" line
│                   │                                  │
│                   │  ETA 14:32             12.4 km   │  ← info row
├───────────────────┴──────────────────────────────────┤
│ SIGNAL LOST - check the phone   (only when silent)    │  ← banner strip
├──────────────────────────────────────────────────────┤
│ ▓▓▓▓▓▓▓▓▓░░░░░░░░░░░░░░░  progress to maneuver       │  ← bottom bar
└──────────────────────────────────────────────────────┘
```

- **States:** waiting screen → *Connected — Start navigation in Google Maps*
  → nav card → **URGENT** (≤ 150 m: amber panel + single chime) →
  **TURN NOW** (≤ 40 m: flashing red panel + urgent chime pattern) →
  *Recalculating...* → arrival pin + chime. `SIGNAL LOST` banner if no BLE
  message for 10 s while navigating.
- **Gestures:** **single tap** = cycle backlight (100 / 55 / 25 %), **double
  tap** = day/night theme toggle. Touch uses default XPT2046 calibration —
  see Troubleshooting if taps land offset.
- **Chimes** are short square-wave beeps on GPIO 26 (LEDC, 50 % duty):
  amber crossing 1×140 ms @ 2.1 kHz; red crossing 3×120 ms @ 2.6 kHz;
  arrival 1×500 ms @ 1.5 kHz. All constants live in
  `platformio/include/config.h` (thresholds, patterns, colors, UUIDs, pin
  map, timing).

---

## 🗂️ Project Structure

```
Android_Auto_Nav_Display/
├── README.md                    ← this document
├── platformio/
│   ├── platformio.ini           ← env crowpanel_28: board + TFT pin build_flags
│   ├── include/
│   │   ├── config.h             ← pins, thresholds, UUIDs, themes timing — tune here
│   │   ├── nav_state.h          ← data model + parser API
│   │   ├── ble_link.h           ← BLE transport API
│   │   ├── ui.h                 ← rendering API
│   │   └── chime.h              ← speaker API
│   ├── src/
│   │   ├── main.cpp             ← wiring: BLE → parser → state machine → UI/chime
│   │   ├── nav_state.cpp        ← ArduinoJson v6 parsing, urgency logic
│   │   ├── ble_link.cpp         ← NUS GATT server, line reassembly, bonding
│   │   ├── ui.cpp               ← nav card, diff redraws, sprites, gestures
│   │   └── chime.cpp            ← non-blocking beep patterns (LEDC)
│   └── .pio/                    ← build output (gitignored)
├── tools/
│   └── test_ble_sender.py       ← PC "fake phone" (bleak) with scripted routes
├── android/                     ← companion app (Kotlin, Android Studio project)
│   ├── settings.gradle · build.gradle · gradle.properties
│   └── app/src/main/
│       ├── AndroidManifest.xml
│       ├── res/values/          ← strings, theme
│       └── java/com/fusionautomate/aanav/
│           ├── MainActivity.kt            ← setup screen (permissions, start/stop)
│           ├── NavNotificationListener.kt ← reads the Maps notification
│           ├── NavParser.kt               ← regex parser → protocol v1 JSON
│           ├── NavBus.kt                  ← in-process event bus
│           ├── BleLink.kt                 ← GATT client, scan→connect→reconnect
│           └── NavForegroundService.kt    ← keeps process alive, 5 s heartbeat
└── docs/
    └── tasker-setup.md          ← no-code companion guide (Option A)
```

---

## ✅ Verification Status (honest numbers)

| Item | Method | Status |
|---|---|---|
| Firmware compiles, 0 errors/warnings | PlatformIO `espressif32@5.3.0`, TFT_eSPI 2.5.0, ArduinoJson 6.21.5 | ✅ Verified on this machine |
| BLE library API usage (`onWrite` two-arg dispatch, `std::string getValue()`, `BLESecurity`, `setMTU`) | Grepped against the exact bundled library source | ✅ Verified |
| Flash/RAM budget | `pio` size check | ✅ 89.6 % flash / 12.5 % RAM |
| LCD bring-up, touch calibration, backlight PWM, chime audibility | **Needs the physical panel** | ⬜ Pending your flash + serial report |
| BLE pairing/reconnect stability | Needs phone | ⬜ Pending |
| Android app builds & runs | Needs Android Studio | ⬜ **Source only — not machine-built yet** |
| Parser against live Maps notifications (locale) | Needs phone + driving | ⬜ Pending |

---

## ⚠️ Disadvantages & Limitations (read before relying on this)

**Fundamental to the approach**
- **No live map.** You get the instruction card, never the map — Maps renders
  only the notification's TBT data (this is why real AA needs H.264).
- **~1 s update latency.** The phone relays the Maps *notification*, which
  updates about once per second while driving. Urgency prompts are computed
  locally on the display from the distance field, so the turn prompt itself
  is not delayed — but a hard brake-to-miss-the-turn event will always lag a
  native head unit.
- **Depends on an unofficial surface.** The Google Maps notification format is
  not a public API; a Maps app update or a language change can break parsing.
  Mitigations: the display shows the raw distance string verbatim, unknown
  maneuvers degrade to "?" / straight-arrow, and the parser is one small
  file. Still — treat this as a hobby device, not a safety system.
- **Phone must stay unlocked-ish state:** some Android skins throttle
  background apps; the app uses a foreground service + you should disable
  battery optimization for it, but aggressive OEM task killers (Xiaomi, Vivo,
  etc.) may still interfere.
- **Phone-only dependency:** no phone / no Maps navigation / no data = no
  display content (by design).

**Hardware**
- **Resistive touch** requires calibration for reliable taps
  (`tft.setTouch` values in `ui.cpp`); it is not a precision interface and
  gestures are deliberately limited to two.
- **Speaker level is modest** — GPIO 26 drives the speaker through a PWM
  square wave; chimes are audible at car-cabin levels but not loud. No voice
  prompts (that would need real DAC audio streaming).
- **No ambient light sensor** — night mode is manual (double tap), not
  automatic.
- **Flash usage is already 89.6 %** of the default app slot; larger features
  require the `huge_app.csv` partition (documented above).
- **USB-C power only for flashing**; in-car you need any 5 V USB adapter or
  the battery connector (3.7–4.2 V per board spec).

**Companion side**
- Tasker route is paid-app + regex-maintenance territory; the dedicated app
  must be sideloaded (no Play Store distribution; a signed APK is planned).
- The app needs Android 8.0+ (26+); BLE permission flow is tuned for
  Android 12+ (`BLUETOOTH_CONNECT`) and Android ≤ 11 (`ACCESS_FINE_LOCATION`
  scan).
- **No iOS support.**

**Safety**
- 🚨 **Driver distraction is a real risk.** Mount the display where a glance
  takes < 1 s, set it up *before* driving, and treat it as a supplement to —
  never a replacement for — the phone's voice guidance and your own attention.
  Check local law on windshield/mounted devices. This project is a DIY
  prototype with no safety certification.

---

## 🧰 Troubleshooting

| Symptom | Fix |
|---|---|
| Blank screen | Verify `platformio.ini` build_flags are intact (pins, `TFT_BL=27`); confirm `ILI9341_DRIVER` — do not overlay a `User_Setup.h` on the library |
| Display never appears in Bluetooth scan | Serial must show `advertising as "AA Nav Display"`; power-cycle; keep the phone's scan screen open |
| Pairing connects/disconnects in a loop | Stale bond from earlier experimental builds: remove **every** "AA Nav Display" entry in the phone, reboot display, pair once |
| Connected but no updates | Start navigation in Maps (the link alone sends nothing); with Tasker check the Intercept profile is on; with the app check the service runs and notification access is granted |
| `SIGNAL LOST` while navigating | Heartbeat stopped — with the app, disable battery optimization; with Tasker, accept ~1 s latency spikes |
| Touch taps land in the wrong place | Replace `calData` in `ui.cpp` `uiInit()` with values from the TFT_eSPI `Touch_calibrate` example, rebuild |
| Chimes inaudible | Confirm a speaker is plugged into the PH2.0-2P connector; raise urgency thresholds/patterns in `config.h`; volume is fixed (PWM duty) |
| Script can't find device | `pip install bleak`; confirm Bluetooth on; try `--address <MAC>`; Windows needs Bluetooth enabled for RFI per-device |
| Android app won't build | Use Android Studio's embedded JDK 17; let Gradle sync finish; check `compileSdk 34` is downloadable in SDK Manager |

---

## 🧠 Implementation Notes (for maintainers)

- **TFT_eSPI pin config** is compile-time in `platformio.ini`; the board is
  the standard CYD 2.8 pinout (MOSI 13, SCLK 14, MISO 12, CS 15, DC 2,
  BL 27, TOUCH_CS 33), matching the proven `Digital_Clock` project.
- **No PSRAM, no full framebuffer.** One 120×190 16-bit sprite (≈45 KB) backs
  the arrow panel for flicker-free flashing; everything else is small diff
  redraws. `TFT_eSprite` derives from `TFT_eSPI`, so glyph helpers render on
  either surface.
- **Colors are RGB565**, defined via the `RGB565(r,g,b)` macro in `ui.cpp`
  from 8-bit triplets — don't paste 24-bit hex color codes into the themes.
- **BLE specifics** (verified against the bundled library in
  `espressif32@5.3.0`): `onWrite(BLECharacteristic*, esp_ble_gatts_cb_param_t*)`
  is the dispatched callback (the one-arg form is only a fallback);
  `getValue()` returns `std::string`; security = `new BLESecurity()` +
  `ESP_LE_AUTH_BOND` + `ESP_IO_CAP_NONE` (Just Works). Bonds are never erased
  and the factory MAC is used — that is the whole stability recipe.
- **Concurrency:** BLE `onWrite` appends into a line buffer guarded by a
  `portMUX` spinlock; parsing and UI run only on the main loop — the parser
  never runs in callback context.
- **Chimes are non-blocking** (millis-based step machine in `chime.cpp`),
  LEDC channel 1; backlight dimming is LEDC channel 0 on GPIO 27.
- **Touch calibration**: the shipped `calData` in `ui.cpp` uses rotation code
  `1` to match `setRotation(1)`; the X/Y values are generic XPT2046 defaults
  and may need per-unit recalibration (TFT_eSPI `Touch_calibrate` example).

---

## 🧪 Testing Strategy

1. **Desk (no car):** flash → pair → `tools/test_ble_sender.py` scenarios →
   verify every state transition + the serial log's `[nav]` lines.
2. **Parser:** feed the notification text you actually receive (Tasker
   `%antext` / app logcat) into the regexes; adjust if your Maps locale
   formats distance differently.
3. **Road:** each milestone ends with a short drive; note notification→screen
   latency near turns before adjusting `URGENT_NEAR_M` / `URGENT_NOW_M`.

## 🚫 Non-Goals

Map rendering / video mirroring · AA wire protocol · audio streaming ·
on-device routing · iOS support · touch→phone control (stretch: NUS TX →
Tasker action).

## 📚 References

- **Primary companion source (this project):** [`android/`](android/) — Kotlin, MIT-ready, notification listener + BLE client.
- **Origin project (architecture + notification parsing):** [maisonsmd/esp32-google-maps](https://github.com/maisonsmd/esp32-google-maps) (MIT) · [write-up](https://maisonsmd.dev/blog/google-maps-on-esp32)
- **Compatible reference firmware/app pair (proven, different maneuver tokens):** [DEMP1993/Drive-assistant-device-firmware](https://github.com/DEMP1993/Drive-assistant-device-firmware) + [DEMP1993/Drive-assistant-android-app](https://github.com/DEMP1993/Drive-assistant-android-app) (PolyForm Noncommercial License 1.0.0)
  - Their app is a working, prebuilt-APK companion (releases at [`Drive-assistant-android-app/releases/latest`](https://github.com/DEMP1993/Drive-assistant-android-app/releases/latest))
  - **BLE compatibility note:** it uses the *exact same service UUIDs* as this firmware: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` (service), `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` (nav write), `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` (char — theirs sends icon bitmap, ours reserves it for notifications); they read `6E400004` (media) and `6E400005` (command notify), which we do not implement
  - **Token mismatch:** they send `turn-right|200 m|Main Street`-style UTF-8 strings with keywords `turn-right`, `turn-left`, `straight`, `roundabout`, `arrive`, `uturn`, `merge`, `sharp-left/right`, `slight-left/right`; our parser currently expects compact codes `TR`, `TL`, `ST`, `RAB`, `ARR`, `UT`, `MEL/MER`, `SHL/SHR`, `SL/SR`. **Patch required** to add keyword→code mapping in `platformio/src/nav_state.cpp` (`maneuverFromString` / `detectManeuver`) if using their app.
  - Note: this firmware has a dedicated arrow panel (they send a 40×40 icon bitmap instead); their app does not use our token format and sends media/command strings our parser ignores.
- [alexanderlavrushko/BLE-HUD-navigation-ESP32](https://github.com/alexanderlavrushko/BLE-HUD-navigation-ESP32) · [jstiefel/esp32_komoot_ble](https://github.com/jstiefel/esp32_komoot_ble)
- True-AA landscape: [WirelessAndroidAutoDongle](https://github.com/nisargjhaveri/WirelessAndroidAutoDongle) · [esp32p4-android-auto](https://github.com/payalneg/esp32p4-android-auto)
- Tasker plugin: [swankdave/Basic-Ble-Writer](https://github.com/swankdave/Basic-Ble-Writer) · [AutoNotification](https://play.google.com/store/apps/details?id=com.joaomgcd.autonotification)
- [Elecrow wiki: 2.8" PlatformIO guide](https://www.elecrow.com/wiki/CrowPanel_ESP32_2.8-inch_with_PlatformIO.html) · [Elecrow wiki: 2.8" Arduino tutorial](https://www.elecrow.com/wiki/ESP32-Display-28-inch-HMI-Arduino-Tutorial.html)
- In-repo references: `../Digital_Clock/platformio/platformio.ini` (pin config) · `../../../CrowPanel - ESP32 HMI Display - 7 Inch/Project/Bluetooth_Stream_Deck/` (BLE bonding recipe)
