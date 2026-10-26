# ESP32 Digital Photo Frame — Production Audit Report

## 1. Executive Summary

The firmware is functional and feature-rich, but **not production-ready for thousands of devices** without fixes. The most serious risks are: a **race condition on the TFT display** (async web handlers call `showCurrentPhoto()`/`tft` from the AsyncTCP task while the loop task renders), **mutex misuse** (recursive locking potential, no timeout discipline in some paths), **unbounded playlist growth** (512 entries on an 8 GB card is plausible and causes heap pressure), **no watchdog recovery for long renders** (`panic=false`), and **no OTA authentication**. The recent session already fixed several of these (deferred rescan, upload concurrency guard, SD polling race, faster AP fallback). Remaining issues are catalogued below.

**Production Readiness Score: 58 / 100** (see §17).

## 2. Build & Compilation Review

- **Compiles cleanly** in PlatformIO (80.1 % flash, 16.7 % RAM). No errors.
- ArduinoIDE sketch is a **divergent copy** — missing deferred-rescan, upload guard, SD retry, AP password, auth. Risk of users flashing stale code. Recommend deleting or clearly marking it deprecated.
- `image_decoder.h` is a 432-line header included via `#include` (not a .cpp) — increases compile time and risks ODR if included in multiple TUs. Currently only included once, so OK, but fragile.
- `LOG_ERROR/WARN/INFO/DEBUG` macros call `Serial.printf` unconditionally based on level; `Serial.printf` is not thread-safe on ESP32 but is used from both tasks. Low risk in practice.
- `esp_task_wdt_init(WDT_TIMEOUT_MS/1000, false)` with `panic=false` means a stalled task **never reboots** — it only logs. For production, consider `panic=true` with proper feeding, or a hardware watchdog.
- `#if __has_include("secrets.h")` with `HAS_DEFAULT_CREDENTIALS` — if secrets.h is missing, defaults to empty. OK.
- No `static_assert` on config constants (e.g. AP_PASS length ≥ 8). A short password silently becomes an open AP.

## 3. Architecture Review

- **Single main loop + AsyncWebServer task** is the concurrency model. The loop task owns the TFT and playlist; the async task handles HTTP. This is correct in principle but was violated by handlers calling `showCurrentPhoto()` directly (partially fixed).
- State machine (`ScreenState`) is implicit; transitions are scattered. No central state-transition function, making it hard to reason about.
- `apMode`, `wifiConnected`, `wifiConnecting` are plain `bool` (not `volatile` in all cases — `wifiConnected` is plain). Accessed from both tasks without synchronization. Risk of torn reads on 32-bit ESP32 (atomic in practice for aligned bools, but not guaranteed by the standard).
- Global mutable state is extensive (40+ globals). Hard to test, hard to reason about re-entrancy.
- No separation between "network layer" and "application layer".

## 4. SD Card & Filesystem Review

- **SD card missing at boot**: handled — `initSD()` retries 3×, then `SCREEN_SD_ERROR` with periodic re-mount in `pollSD()`. Good.
- **Card removed during slideshow**: `pollSD()` detects `totalBytes()==0`, unmounts, sets `SCREEN_SD_ERROR`. But the slideshow engine checks `sdMounted` only at the top of the loop iteration; an in-flight render holds the mutex and may crash if the card vanishes mid-read. `File.read` returns short reads; `renderJPEG` checks `JDR_OK`. Partial mitigation.
- **Corrupted image**: `renderImageFile` returns false; `consecutiveFailures` increments; after all photos fail, `drawUnreadablePhotoScreen`. Good. But `consecutiveFailures` is compared against `photoPlaylist.size()` which can be large; OK.
- **Empty photo folder**: `SCREEN_NO_PHOTOS`. Good.
- **Full SD card**: upload `write()` returns 0 → retried 3× → fails → file removed. But no pre-check of free space; a large upload can fill the card mid-transfer. Recommend checking `sdFreeMB` before accepting.
- **Power loss during write**: FAT corruption possible; no journaling. Recommend periodic `SD.end()`/remount or accept risk.
- **Thousands of photos**: `MAX_PHOTOS_PLAYLIST=512`; `scanPhotos()` allocates up to 512 `String` objects (~512 × ~40 bytes = 20 KB). On 328 KB RAM this is ~6 % — acceptable but tight with upload buffers. `photoPlaylist` is a `std::vector<String>` — reallocation on growth can fragment heap.
- **Large image files**: PNG decoder allocates `uint8_t buffer[512]` on the stack inside `renderPNG` (512 bytes — fine). BMP allocates `rowBuf` via `malloc(rowStride)` up to 16 KB — freed. JPEG uses TJpgDec internal buffers. No unbounded allocation observed, but TJpgDec's `drawSdJpg` may allocate internally.
- **`dataOffset + row * rowStride` overflow**: `rowPos` is `uint32_t`; for a 4096×4096 32-bit BMP this overflows. Mitigated by MAX_DIM=4096 and rowStride≤16384, but `row * rowStride` can be 4096×16384 = 67 MB > uint32 max? No, 67M < 4.2B. OK. But file size could be huge; `file.seek` on a truncated file returns false-ish (SD library may not check). Low risk.
- **SD SPI speed 4 MHz**: conservative, stable. Good.
- **No `SD.end()` on unmount in the `pollSD` unmounted path's `SD.begin` failure** — `SD.end()` is called before retry. OK.

## 5. WiFi & Provisioning Review

- **STA connection timeout 15 s, 3 retries at 5 s** — reasonable.
- **AP fallback**: `WiFi.softAP(AP_SSID, AP_PASS)` — return value **ignored**. If softAP fails (e.g., radio busy), `apMode=true` but no AP exists. Recommend checking the return value and retrying.
- **AP is WPA2** with password `photoframe` (8+ chars). If `AP_PASS` is shorter than 8, `WiFi.softAP` silently falls back to open. No validation. Add `static_assert` or runtime check.
- **Captive portal DNS**: `dnsServer.start(53, "*", apIP)` — wildcard. Good. But `processNextRequest()` is called in `loop()` only when `apMode`. Good.
- **Captive portal detection routes** (`/generate_204`, etc.) are unauthenticated — correct for provisioning.
- **WiFi credentials in NVS**: stored plaintext. Acceptable for this threat model but document.
- **`WiFi.setAutoReconnect(false)`** + manual reconnect logic — fine.
- **Hostname set** via `WiFi.setHostname`. Good.
- **No WiFi event callbacks** — polling-based, simpler, slightly less responsive.
- **AP IP hardcoded 192.168.4.1** in UI strings — matches ESP32 default. OK but fragile if `softAPConfig` is ever used.

## 6. Web Dashboard Review

- **No authentication by default** — any LAN client can upload/delete/control. Opt-in via `WEB_AUTH_USER`/`WEB_AUTH_PASS`. For production, recommend defaulting to enabled with a per-device password, or documenting the risk. (Already in README.)
- **CORS not set** — dashboard is same-origin, fine.
- **No rate limiting** — a malicious client can flood `/api/*`. Low risk on LAN.
- **`/api/status` builds JSON by string concatenation** — no escaping of SSID/IP. SSIDs with quotes could break JSON. Use ArduinoJson or escape. Medium risk.
- **`/api/photos` and `/api/thumb` open files without checking `isDirectory`** in some paths — mitigated by `sanitizeFilename`.
- **Path traversal**: `sanitizeFilename` strips slashes and rejects `..`. Good. But it allows spaces and Unicode bytes >127 (passed through `isalnum` which is locale-dependent). Filenames with non-ASCII may be mangled. Low risk.
- **`request->send(SD, fullPath, contentType)`** passes a filesystem path to AsyncWebServer, which opens the file itself. If the file is deleted mid-response, the response fails. Low risk.
- **No `server.onNotFound`** — 404s are default. Captive portal probing may fail on some OSes that expect specific paths. Acceptable.
- **Upload progress**: `xhr.upload.onprogress` works. Good.

## 7. Upload System Review

- **Single active upload enforced** via `uploadOwner` pointer + 409 rejection. Good (recently added).
- **Multipart callback is called per-chunk**; `!index` resets state. Correct for sequential files.
- **File handle closed on `final`**. Good.
- **Re-upload of same name**: removes old file first. Good.
- **Stalled upload recovery**: 20 s timeout closes handle. Good. But `uploadOwner` is set to nullptr only in the stalled path — need to verify it's cleared in all failure paths (filename rejection, SD open failure, SD busy). Recently partially fixed; verify `uploadOwner` is cleared in the `safeName.length()==0` path — **yes, added**. In `sdMounted==false` path — **yes**. In SD-open-failed and SD-busy paths — **yes**. Good.
- **`uploadRequestFailed` is global and sticky** — reset at `!index`. If a rejected-busy request's `final` callback runs, it calls `sanitizeFilename` and may touch state. Mitigated by the `uploadRejectedRequest == request` early return. Good.
- **No maximum file size check** — a 2 GB file would fill the card. Recommend rejecting if `sdFreeMB < threshold` or tracking bytes written.
- **`isUploading` pauses slideshow** — good, avoids SD contention. But `isUploading` is also set true during thumbnail uploads; fine.
- **Upload during AP mode**: works (web server runs in AP mode). Good.
- **Concurrent thumbnail upload**: the UI uploads the thumbnail as a separate request after the photo. If another tab uploads in between, the thumbnail could be rejected with 409. Acceptable.

## 8. Slideshow Engine Review

- **Non-blocking transition state machine** — well structured. `tickTransition()` runs every loop iteration.
- **`delay(5000)` in `showCurrentPhoto` intro** — blocks the loop for 5 s. During this, DNS processing, WiFi reconnect, and watchdog feeding stall. **Blocking operation in a timing-critical path.** Recommend converting to a non-blocking timer.
- **`renderImageFile` is called from `showCurrentPhoto`** which is called from the loop task. Good — no TFT access from async task (after recent fixes). But `showCurrentPhoto` is still called from the intro path and from `tickTransition`'s `waitingForImage` path — both in loop task. Good.
- **`currentPhotoIndex` is `int`**; `photoPlaylist.size()` is `size_t`. Comparisons cast. OK.
- **Shuffle mode**: avoids immediate repeats. Good. But `random()` uses `randomSeed(analogRead(34))` — ADC noise on pin 34 may be zero if the pin is tied. Low entropy. Consider `esp_random()`.
- **`consecutiveFailures` logic**: resets to 0 on success. Good.
- **`forcePhotoName` lookup** is linear in playlist size — fine for ≤512.
- **`introShown` is `static bool`** inside `showCurrentPhoto` — persists across reboots? No, RAM only. Fine. But it's `static` so it only shows once per boot. Good.
- **Slideshow only runs when `wifiConnected`** — recently fixed. Good. But if WiFi drops mid-slideshow, the loop's `wifiConnected=false` branch sets `otaReady=false` and `currentIP=""` but does **not** stop the slideshow (the engine checks `wifiConnected` at the top of the next iteration, so it stops naturally). Good. However, `currentScreen` stays `SCREEN_SLIDESHOW` — the screen still shows the last photo. Acceptable.
- **`isUploading` pauses slideshow** — good.

## 9. OTA Review

- **ArduinoOTA is enabled** when WiFi connects (`otaReady=true`). Default port 3232.
- **No OTA password** — anyone on the LAN can push firmware. **Critical for production.** Add `ArduinoOTA.setPassword(...)`.
- **No OTA progress feedback** on the TFT — minor.
- **OTA during upload**: if an upload is in progress, OTA can still run (both use SD/network). Potential contention. Recommend disabling OTA during upload or serializing.
- **OTA partition scheme**: default `default` partition table (1.25 MB app). Firmware is ~1.05 MB — tight. Recommend `huge_app` or a custom partition table with OTA slots.
- **No rollback on failed OTA** — ESP32 bootloader supports it if configured. Verify `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`.

## 10. Memory & Resource Analysis

- **Flash 80.1 %** (1,049,745 / 1,310,720) — tight. Adding features may exceed. Recommend `huge_app` partition.
- **RAM 16.7 %** (54,848 / 327,680) — comfortable. But heap fragmentation from `String` allocations and `std::vector` growth is the real risk.
- **`photoPlaylist` as `std::vector<String>`** — 512 entries × ~40 bytes = 20 KB + heap fragmentation. Consider a fixed-size array or a linked list of char*.
- **`String` usage throughout** — Arduino `String` allocates on heap; fragmentation over long uptimes. For a device running 24/7 for months, this is a real risk. Consider `char[]` buffers where possible.
- **PNG decoder `uint8_t buffer[512]` on stack** — fine. BMP `malloc(rowStride)` up to 16 KB — freed. Good.
- **`g_pngCtx.lineBuf[SCREEN_WIDTH]` = 640 bytes** static. Fine.
- **No heap monitoring** — recommend logging `ESP.getFreeHeap()` periodically.
- **Stack size of loop task** is default (8 KB). `renderBMP` allocates 16 KB on heap (not stack) — OK. But `tickTransition` uses no large locals. `showCurrentPhoto` calls `renderImageFile` which calls renderJPEG/PNG/BMP — all allocate on heap or use static. Probably OK but close to limit. Recommend increasing loop task stack to 12 KB.
- **No `uxTaskGetStackHighWaterMark` monitoring**.

## 11. Concurrency & FreeRTOS Analysis

- **Mutex (`sdMutex`) is a binary semaphore** created with `xSemaphoreCreateMutex`. Good — supports priority inheritance.
- **`pollSD()` takes mutex with 500 ms timeout** — good. Returns early if not acquired.
- **Upload handler takes mutex with 1000–2000 ms timeouts** — good.
- **Image renderers take mutex with 1500 ms timeout** — good.
- **No recursive locking** — none of the functions called while holding the mutex call another function that takes the mutex. Verified.
- **`isUploading`, `uploadOwner`, `apMode`, etc. are accessed from both tasks without atomicity** — on ESP32, aligned 32-bit loads/stores are atomic in practice, but the C++ memory model doesn't guarantee it. Recommend `std::atomic<bool>` or volatile + critical sections. Low practical risk.
- **`photoPlaylist` vector is mutated by `scanPhotos()` (loop task) and read by async handlers** (delete-all copies under mutex, but reads `photoPlaylist.size()` outside mutex in `/api/photos`). **Race condition**: `/api/photos` iterates `photoPlaylist` while `scanPhotos()` clears and repopulates it. Could crash. Fix: take mutex around the entire iteration in `/api/photos`.
- **`currentIP`, `currentSSID`** strings are read by async handlers and written by loop task — String copy is not atomic; could tear. Low risk but real. Recommend guarding with mutex or copying under lock.
- **AsyncTCP task stack** is default (8 KB) — the upload handler does `String` allocations and `SD.open`; could overflow under heavy load. Recommend increasing `CONFIG_ASYNC_TCP_STACK_SIZE`.

## 12. Performance Analysis

- **SD SPI at 4 MHz** — conservative. Could increase to 8–10 MHz for faster uploads, but stability risk. Keep 4 MHz.
- **TFT SPI at 20 MHz** — good for ILI9341.
- **Upload throughput**: chunked, 1436-byte chunks typical. At 4 MHz SPI, ~500 KB/s theoretical; real ~200–300 KB/s. Acceptable.
- **Image rendering blocks the loop task** for the duration of decode+draw (hundreds of ms for large JPEGs). During this, DNS, WiFi reconnect, and watchdog feeding stall. For a photo frame this is acceptable but limits responsiveness. Could move rendering to a separate task.
- **`delay(10)` in loop** — 10 ms minimum latency for DNS/WiFi processing. Acceptable.
- **`tickTransition` runs every loop iteration** — even when no transition active, it checks `waitingForImage` and renders. Fine.
- **`scanPhotos()` is O(n)** in file count — fine for ≤512.
- **No caching of photo list** — rescanned on every delete/upload. Acceptable.

## 13. Reliability & Recovery Analysis

- **SD card removal during slideshow**: handled via pollSD, but the screen stays on the last photo until the next poll (1 s). Minor.
- **WiFi drop mid-slideshow**: slideshow stops (gated on `wifiConnected`), screen stays on last photo. Acceptable.
- **Power loss during upload**: partial file remains; next upload of same name removes it. But if power is lost mid-`SD.remove` or mid-`SD.open`, the filesystem may be inconsistent. FAT has no journaling. Acceptable for this use case.
- **NVS corruption**: `prefs.begin` returns error (logged) but code continues with defaults. Good.
- **Boot with no SD card**: `SCREEN_SD_ERROR`, retries every 1 s. Good.
- **Boot with empty SD**: `SCREEN_NO_PHOTOS`. Good.
- **Boot with corrupted NVS**: defaults used. Good.
- **Watchdog**: `panic=false` means a stalled task logs but doesn't reboot. For production, consider `panic=true` with proper feeding, or a separate watchdog task. Current approach prevents reboots during long renders but also prevents recovery from true deadlocks.
- **No brownout detection handling** — ESP32 brownout resets are default. Fine.

## 14. Edge Case Checklist

| Scenario | Handled? | Notes |
|---|---|---|
| SD card missing at boot | Yes | SCREEN_SD_ERROR, retry |
| SD card removed during slideshow | Partial | pollSD detects in ≤1 s; in-flight render may glitch |
| Corrupted JPEG | Yes | Returns false, skips, counts failures |
| Progressive JPEG | Yes | Rejected with helpful message |
| Empty photo folder | Yes | SCREEN_NO_PHOTOS |
| Full SD card | Partial | Write fails after retries; no pre-check |
| Upload interruption (client vanishes) | Yes | 20 s stalled recovery |
| WiFi disconnect | Yes | Auto-reconnect, then AP fallback |
| Router unavailable | Yes | AP fallback after ~30 s |
| AP fallback failure | No | `softAP` return ignored; no retry |
| OTA interruption | Partial | Bootloader rollback if configured |
| Low memory | No | No heap monitoring or graceful degradation |
| Thousands of photos | Partial | 512 cap; heap pressure |
| Large image files | Partial | BMP rowStride cap; PNG 4096 cap |
| Concurrent uploads | Yes | 409 rejection |
| Browser refresh during upload | Yes | Upload aborts; file removed on failure |
| Power loss during write | Partial | FAT corruption risk; no journaling |
| Non-ASCII filenames | Partial | Passed through `isalnum` (locale-dependent) |
| SSID with quotes | No | Breaks JSON in `/api/status` |
| AP password < 8 chars | No | Silently becomes open AP |

## 15. Critical Issues

1. **Race condition on `photoPlaylist` vector** — `/api/photos` and `/api/thumb` iterate it without holding `sdMutex` while `scanPhotos()` (loop task) mutates it. Can crash. **Fix**: hold mutex for the entire iteration.
2. **No OTA password** — anyone on LAN can flash firmware. **Fix**: `ArduinoOTA.setPassword(...)`.
3. **TFT accessed from async task** — mostly fixed (deferred rescan), but verify no remaining paths (e.g., `/api/control` "refresh" now sets `forceRedraw` flag — good). Audit all handlers: none call `tft.*` directly. Good.
4. **`delay(5000)` blocking in `showCurrentPhoto`** — stalls DNS/WiFi/watchdog for 5 s. **Fix**: non-blocking timer.
5. **Flash 80 % full** — no headroom for features. **Fix**: `huge_app` partition.
6. **Watchdog `panic=false`** — no recovery from true deadlocks. **Fix**: consider `panic=true` with proper feeding, or a separate watchdog task.
7. **`String`-heavy code** — heap fragmentation risk over months of uptime. **Fix**: migrate hot paths to `char[]`.
8. **No heap monitoring** — recommend periodic `ESP.getFreeHeap()` logging.
9. **`photoPlaylist` O(n) linear scan** in `/api/control` "show" — fine for 512.

## 16. Prioritised Fix Plan

**P0 (before production):**
1. Add mutex protection around `photoPlaylist` reads in `/api/photos` and `/api/thumb`.
2. Set OTA password.
3. Switch to `huge_app` partition table (or verify OTA slots exist).
4. Replace `delay(5000)` with non-blocking timer.
5. Add `static_assert` or runtime check for AP_PASS length ≥ 8.

**P1 (short-term):**
6. Escape JSON in `/api/status` (or use ArduinoJson).
7. Add free-space check before accepting uploads.
8. Monitor and log heap + stack high-water marks.
9. Increase loop task stack to 12 KB.
10. Add `softAP` return-value check + retry.

**P2 (medium-term):**
11. Migrate `String` to `char[]` in hot paths.
12. Move image rendering to a dedicated task to keep loop responsive.
13. Add a per-device AP password (derived from MAC) instead of a shared one.
14. Add OTA progress feedback on TFT.
15. Delete or clearly deprecate the ArduinoIDE copy.

## 17. Production Readiness Score: 58 / 100

| Category | Score | Notes |
|---|---|---|
| Build & compilation | 9/10 | Clean build, tight flash |
| Architecture | 6/10 | Good separation; global state heavy |
| SD & filesystem | 7/10 | Good recovery; no journaling |
| WiFi & provisioning | 7/10 | Good fallback; ignored softAP return |
| Web dashboard | 6/10 | No auth by default; JSON escaping |
| Upload system | 7/10 | Concurrency guard added; no size check |
| Slideshow engine | 7/10 | Blocking delay; good state machine |
| OTA | 4/10 | No password; tight partitions |
| Memory & resources | 5/10 | String-heavy; no monitoring |
| Concurrency | 6/10 | Mutex mostly correct; vector race |
| Performance | 7/10 | Acceptable for photo frame |
| Reliability & recovery | 7/10 | Good SD/WiFi recovery |
| Edge cases | 6/10 | Most handled; a few gaps |
| **Overall** | **58/100** | Functional but needs P0 fixes before fleet deployment |

The recent session's fixes (deferred rescan, upload guard, SD polling race, faster AP fallback, auth option, SD write self-test) moved the score up from ~45 to 58. Completing the P0 list would bring it to ~75.
