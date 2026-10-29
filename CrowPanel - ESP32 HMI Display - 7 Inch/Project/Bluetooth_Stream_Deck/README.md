# Bluetooth Stream Deck

A Bluetooth LE HID keyboard Stream Deck for the Elecrow CrowPanel 7-inch ESP32-S3 display. It keeps the WiFi project's 800x480 screen, touch input, and 4x4 tile layout. When you tap a tile, the ESP32 sends a keyboard or media-key command directly to the paired computer. No WiFi connection or companion server is needed.

## What you need

- Elecrow CrowPanel 7-inch ESP32-S3 (N4R8) with capacitive touch
- USB data cable connected to the CH340/UART port for flashing
- A Windows, macOS, or Linux computer with Bluetooth
- VS Code and the PlatformIO IDE extension

## Open and build in VS Code

1. Install **Visual Studio Code**.
2. In VS Code, open **Extensions** and install **PlatformIO IDE**.
3. Choose **File → Open Folder** and select this `Bluetooth_Stream_Deck` folder, the one containing `platformio.ini`.
4. Wait for PlatformIO to install the ESP32 tools and libraries. The first build can take a few minutes.
5. Connect the display to the computer using its **CH340/UART USB port**. Find the COM port in **PlatformIO → Devices** or Windows Device Manager.
6. In a VS Code terminal opened in this folder, build the firmware:

   ```powershell
   python -m platformio run -e crowpanel_7
   ```

7. Upload it, replacing `COM4` with the port for your display:

   ```powershell
   python -m platformio run -e crowpanel_7 -t upload --upload-port COM4
   ```

   Or run `./flash.ps1`; it builds the firmware and asks which COM port to use.

Use the CH340/UART port, not the native USB port. The native USB pins are shared with the touch controller on this display. The serial monitor uses **115200** baud.

## Pair with your computer

The deck uses the ESP32's factory Bluetooth address, which never changes, and it never deletes its pairing keys. Both sides keep the bond, so reconnects after a reboot are automatic and stable.

Older builds generated a new random address or erased bonds, which left the computer retrying a dead pairing — that is the connect/disconnect loop. After flashing this version, clear out every leftover entry once:

1. In Windows **Settings → Bluetooth & devices**, remove **every** **BT Stream Deck** entry (older builds can have left several).
2. Restart the display so it advertises again.
3. Choose **Add device**, select **BT Stream Deck**, and complete pairing (no passkey prompt — it pairs silently with Just Works).
4. The screen status changes to **Bluetooth ready**. Tap a tile to send its command.

After this one-time cleanup, the pairing survives reboots on both sides. If it is not listed, wait a few seconds and scan again.

## Tile actions

The default shortcuts use standard keyboard and media-key reports. Some application-specific actions may differ by operating system or active app.

| Tile | Command sent |
|---|---|
| Mute, Play/Pause, Next, Vol Down, Vol Up | Standard media keys |
| Mic | Ctrl+D (app-dependent microphone mute shortcut) |
| Esc | Escape |
| Snip | Windows+Shift+S |
| Copy / Paste | Ctrl+C / Ctrl+V |
| Desktop | Windows+D |
| Terminal | Windows+R, type `wt`, press Enter |
| Lock | Windows+L |
| Start | Windows key |
| Alt+Tab / Ctrl+Tab | Standard keyboard shortcuts |

Edit the mapping in `send_action()` in `src/main.cpp`. Commands like Snip, Desktop, Terminal, and Lock use Windows shortcuts; change these mappings if using another operating system. Media keys work where supported by the computer and application.

The screen dims its backlight after one minute without a touch and restores it on the next touch.

## Limitations and security

- This firmware is BLE-only: it does not use WiFi and does not include a companion server. A previously copied WiFi `server/` directory was removed; use `WiFi_Stream_Deck` if you need the HTTP dispatcher or the diary/notes tab.
- The BLE keyboard uses plain bonding without MITM (Just Works). The display has no passkey screen/keypad, so MITM/passkey pairing is not supported; requiring it is what made Windows abort pairing and cycle connect/disconnect. After first pairing, the ESP32 and Windows retain their matching bond keys for reconnection.
- The deck always uses the ESP32's factory public Bluetooth address and never erases its bonds, so the pairing is stable across reboots. Erasing the whole flash (`-t erase`) deletes the ESP32-side bond; remove the device in Windows and pair again afterwards.
- A paired computer can type arbitrary keys through the deck. Only pair with a computer you control, and remove the pairing when the display is not in use.
- The Windows-specific tiles (Snip, Desktop, Terminal, Lock) send Windows shortcuts. On macOS or Linux those tiles send keys the OS may ignore; edit `send_action()` for local shortcuts.
- The Terminal tile types `wt` (Windows Terminal). If Windows Terminal is not installed, edit that branch or install it.

## Troubleshooting

| Symptom | Fix |
|---|---|
| Bluetooth Stream Deck does not appear | Confirm the display is powered on, wait a few seconds, then scan again. The serial log must show `BLE: advertising running`. |
| Serial log shows `GATT connected` / `GATT disconnected` cycling every few hundred milliseconds | The computer is retrying a pairing the deck no longer has keys for (left over from older builds). Remove **every** **BT Stream Deck** entry in Windows Bluetooth settings, restart the display, and pair once. |
| Serial log shows `pairing FAILED (fail reason ...)` | Note the reason. 102 (`SMP_CONN_TOUT`) means the link dropped mid-pairing — almost always a stale entry on the computer side; remove old entries and pair again. |
| Screen works but taps do nothing | Check that the computer is paired and the status says **Bluetooth ready**. |
| No COM port | Try another USB cable that supports data and connect to the CH340/UART port. |
| Upload fails | Close Serial Monitor and other programs using the COM port, then upload again. |
| Blank screen | Check that the PlatformIO project uses the included board definition `boards/esp32-s3-devkitc1-n4r8.json`. |
| Touch stays dead on a V3.0 board | Set `FORCE_TOUCH_RESET=1` in `platformio.ini`, rebuild, and reflash. This forces the GT911 reset through the PCA9557 expander on every boot. |
| Pairing lost after erasing flash | `-t erase` deletes the ESP32-side bond but not the computer's. Remove the device in Windows and pair again. |

## Project files

- `src/main.cpp` — screen, touch input, UI, and Bluetooth keyboard actions
- `platformio.ini` — PlatformIO board, build, and library settings
- `boards/` — required CrowPanel ESP32-S3 board definition
- `include/lv_conf.h` — LVGL configuration
- `flash.ps1` — Windows build-and-upload helper

This is a separate project from `WiFi_Stream_Deck`. The WiFi project sends tile IDs to a PC server; this project sends Bluetooth keyboard and media-key events directly to the paired computer.

## Development checks

Build the firmware with PlatformIO:

```powershell
python -m platformio run -e crowpanel_7
```