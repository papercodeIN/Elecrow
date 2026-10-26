# Production-Readiness Audit — Digital Photo Frame (CrowPanel 7.0" ESP32-S3, 800×480)

**Scope:** `PlatformIO/src/main.cpp`, `include/{index_html.h, image_decoder.h, display_7inch.h, config.h}`, `platformio.ini`, `boards/esp32-s3-devkitc1-n4r8.json`.
**Audit date:** 2026-10-26 · **Firmware:** base commit `5750c71` · **Build env:** `esp32s3_7inch`, espressif32@6.9.0 (Arduino core 2.0.17), LovyanGFX, ESPAsyncWebServer 3.6.0 / AsyncTCP 3.3.2.

---

## 1. Executive Summary

The firmware is functional and field-proven for its happy path: uploads from phone and laptop work reliably (PSRAM-buffered, single-shot SD write), the slideshow runs all 12 transitions, settings persist in NVS, and the AP fallback provisions Wi-Fi safely. The core SD-contention and upload-corruption bugs that plagued earlier builds have been fixed and verified.

However, a full production audit found **3 Critical, 8 High, 13 Medium, and 7 Low** issues. The three most consequential:

1. **OTA is dead on arrival** — the partition table (`huge_app.csv`) has no second app slot.
2. **`/api/view` and `/api/thumb` stream from SD with no mutex** — asynchronous responses race the loop task's SD/SPI access (exactly the class of corruption already fixed elsewhere).
3. **Stored XSS via SD-side filenames interpolated into inline JS** — filenames that didn't pass the upload sanitizer (e.g., copied directly onto the card via PC) can execute script in the dashboard.

None of these affect daily use until those specific features are exercised, so the device remains fully usable today. See §16 for the prioritised fix plan and §17 for the readiness score.

---

## 2. Build & Compilation Review

**Verified OK:**
- Builds clean: `pio run -e esp32s3_7inch` → SUCCESS. No errors, no warnings promoted.
- `-DARDUINO_USB_CDC_ON_BOOT=0 -DARDUINO_USB_MODE=1` correctly routes `Serial` to UART0 (fixes silent COM4 serial).
- PSRAM flags correct: OPI PSRAM enabled via board json (`qio_opi` flash/psram mode), `-DBOARD_HAS_PSRAM`.
- Library versions compatible (espressif32 6.9.0 + AsyncTCP 3.3.2 pair is a known-good combo).
- `secrets.h.example` committed; `secrets.h` git-ignored — no credential leakage.

**Findings:**

| # | Sev | Location | Issue |
|---|-----|----------|-------|
| B1 | **Critical** | `boards/esp32-s3-devkitc1-n4r8.json` | `partitions: huge_app.csv` → single 3 MB app slot, **no `app1`/`otadata`** → `esp_ota_begin()` always fails; every OTA path advertised in the UI is dead. |
| B2 | Low | `main.cpp` | Duplicate forward declarations of helpers; cosmetic. |
| B3 | Low | `main.cpp:73` region | `uploadHandlerActive` defined in two places historically; currently resolved, but the pattern (globals declared in both a header-like cluster and main) invites recurrence — consolidate all shared state declarations in one block. |
| B4 | Low | build flags | No `-Wall -Wextra`; latent warnings (signed-char UB, unused vars) are invisible. Add `-Werror=return-type -Wall` to `build_flags`. |

**B1 patch** — `platformio.ini`:
```ini
board_build.partitions = partitions_4mb_ota.csv
```
`partitions_4mb_ota.csv`:
```
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x6000
otadata,  data, ota,     0xF000,  0x2000
app0,     app,  ota_0,   ,        0x1A0000
app1,     app,  ota_1,   ,        0x1A0000
spiffs,   data, spiffs,  ,        0xA0000
```

---

## 3. Architecture Review

**Current design:** single Arduino loop task (core 1) for slideshow/rendering/SD polling; AsyncTCP task for all web work; one global `sdMutex` gating the SPI/SD bus; LovyanGFX RGB panel on DMA; NVS for settings; global-string state machine for boot/WiFi screens.

**Strengths:** simple, single-owner state; the mutex discipline for uploads and pollSD is now correct; NVS persistence is complete; screen-state machine (`SCREEN_*`) is linear and readable.

**Weaknesses (architectural):**

| # | Sev | Issue |
|---|-----|-------|
| A1 | High | Two execution contexts (loop task + AsyncTCP task) both touch SD/TFT/shared strings with point-fix mutexes rather than a single command queue. Async handlers that need rendering/SD should post a "pending action" flag consumed by the loop, not act directly. Several High findings below (H1, H2) are instances of this. |
| A2 | Medium | Slideshow render and upload watchdog share `lastUploadActivity` timestamps — coupled clocks make reasoning about timing fragile. Dedicate a `uploadState` enum (idle/streaming/writing/failed) instead of flag-and-timestamp combinations. |
| A3 | Medium | No auth layer at any route (M7) — realistically acceptable for a LAN appliance but should at least be an option. |

---

## 4. SD Card & Filesystem Review

**Verified OK:**
- `pollSD()` mounted-branch health check is mutex-guarded; remount path (`sdMounted=false` → loop remounts) works after failed uploads.
- `sanitizeFilename()` blocks directory traversal (`..`, path separators stripped; only `[A-Za-z0-9._-]` survive).
- `scanPhotos()` caller-lock discipline correct — never called while the caller holds `sdMutex`.
- SD pins (MOSI 11, MISO 13, SCK 12, CS 10) verified against Elecrow's official factory code.

**Findings:**

| # | Sev | Location | Issue & fix |
|---|-----|----------|-------------|
| S1 | **High** | `main.cpp` /api/photos handler | Holds `sdMutex` while opening **every** photo file to stat it → with N photos the async task stalls O(N·openTime), starving the slideshow and other handlers. Fix: list the directory (names + lastModified only) without opening files; store size at scan time; use `String` reserve or `char` buffers to cut churn. |
| S2 | **High** | pollSD unmounted branch | Probing calls (`SD.begin`, `SD.cardSize`) run **without** the mutex while an upload/view can be mid-bus-operation on another task. Fix: take `sdMutex` (short timeout) around the probe. |
| S3 | Medium | mount path | `/photos` (`mkdir`) only attempted in one place; after a reinserted card whose FAT is dirty the remount can succeed but leave the dir missing → uploads 500. Call `mkdir` after every successful `SD.begin()`. |
| S4 | Medium | upload final | Full-card scenario: `SD.open()` succeeds but writes fail mid-way; handled (rollback + remount) but the remount on a **full** card loops forever burning CPU every 2 s. Cap remount attempts (e.g. 5) then show the SD error screen once, with a periodic cheap re-probe. |
| S5 | Medium | /api/thumb | Missing thumb returns **503 "Generating"** instead of 404/generate-on-demand; callers can't distinguish "no thumb possible" from "device busy". Fix: if the source photo exists, generate the thumb synchronously once (mutex-held, bounded); else 404. |
| S6 | Low | thumbnails | Thumbs serialize full images (auto-convert sizes them, but no explicit downscale). Encode thumbs at ≤200 px width during scan to cut `min()`/decode cost for galleries. |

**Failure-scenario results:** SD missing at boot → SD error screen + retry loop ✔; removed mid-slideshow → pollSD detects → SD error screen, recovers on reinsert ✔ (subject to S2 race); corrupted JPEG → `consecutiveFailures` → skip + eventual "unreadable" screen, recovery OK when good photos exist ✔; empty folder → "No Photos" screen with URL ✔; full card → upload fails cleanly (see S4 caveat) ✔.

---

## 5. WiFi & Provisioning Review

**Verified OK:** AP+STA simultaneous mode; background retry of saved credentials while AP runs; the setup screen shows the dashboard IP; reconnect logic handles router-down when it returns later (retry loop keeps running).

**Findings:**

| # | Sev | Location | Issue & fix |
|---|-----|----------|-------------|
| W1 | Medium | /api/wifi handler | `delay(1500)` + `ESP.restart()` **inside an async handler** → blocks the AsyncTCP task 1.5 s mid-HTTP, breaking the response and any concurrent connections. Fix: set `volatile bool restartPending = true;`, return JSON **first**, perform restart in `loop()` ~500 ms later. |
| W2 | Medium | AP lifecycle | DNS (captive) server never stopped and `softAP()` never disconnected after successful STA join → the AP stays permanently active with a stale DNS. Fix: on `WiFi.status()==CONNECTED` and after provisioning grace, `dnsServer.stop(); WiFi.softAPdisconnect(true);` (keep AP only while creds are being entered, if desired as a fallback policy then document it as intentional). |
| W3 | Medium | OTA lifecycles | `ArduinoOTA.begin()` is called but there is **no `ArduinoOTA.end()`** on Wi-Fi mode transitions; mDNS likewise. On every mode flip, re-init both or end/re-begin symmetrically. |
| W4 | Medium | secrets | Empty `OTA_PASSWORD` ⇒ OTA accepts unauthenticated flash from anyone on the LAN. Fail the build at compile time if it's empty: `#if !defined(OTA_PASSWORD) || OTA_PASSWORD[0] == '\0'\n#error "Set OTA_PASSWORD in secrets.h"\n#endif` |
| W5 | Low | AP naming | Open AP `PhotoFrame-Setup` with `AP_PASS=""`: anyone in range can join during provisioning. Set a per-device passcode (derive from MAC) even for the fallback AP. |

---

## 6. Web Dashboard Review

**Verified OK:** `/` served with charset; status polling and settings saves don't clobber each other; upload XHR timeouts sensible (120 s); blob URLs for preview revoked; `uploadBusy` prevents double-start; frontend/backend param contract (`interval/order/fit/transition/paused/brightness`) matches.

**Findings:**

| # | Sev | Location | Issue & fix |
|---|-----|----------|-------------|
| X1 | **High** | `index_html.h` ~769–781 | **Stored XSS.** Gallery cards build `onclick="showPhotoNow('${escapeHtml(p.name)}')"` with `innerHTML`. `escapeHtml` is an HTML-escape, not a JS-string escape: the HTML parser **decodes `&#39;` back to `'` before JS parsing**, and `\` is not escaped, so `\';…` breaks out of the JS literal. Direct filenames (never passed through upload sanitize — files copied by PC) become stored script that hijacks the uploader (could exfiltrate Wi-Fi passwords via /api/status). **Fix (both sides):** (a) server-side: also escape in `/api/photos` JSON with a `jsonEscape()`; (b) client-side: never interpolate names into inline JS — use `data-name` attributes + **delegated listeners**, or at minimum a real JS-string escaper. Patch below. |
| X2 | Medium | main.cpp:1194 | `/api/photos` JSON construction doesn't JSON-escape names — combines with X1; escape server-side regardless of client fix. |
| X3 | Medium | gallery | `hasThumb` from server ignored → gallery serializes `/photos/<name>` full images ("thumbs") 1-per-12 s on slow networks. Use the `.thumb.jpg` endpoint when `hasThumb`. |
| X4 | Medium | fetch URLs | `&_=Date.now()` cache-buster defeats the server's ETag/304 caching on every photo fetch — wasteful on cellular-hotspot/phone connections. Trust 304s: drop the buster for `GET /photos/...` (keep for API calls that mutate). |
| X5 | Medium | polling | Offline device: overlapping status requests with 2 s interval + backoff cap (2 s) that is < the server's 20 s upload window → toast spam and request pile-up. Raise offline backoff to ~10 s → 18 s, dedupe with an `inFlight` flag, and show an offline badge instead of toasts. |
| X6 | Low | toast | Shared timer variable clobbered by rapid toasts → earlier toasts never disappear. Store per-toast timer or use a single rotating toast. |
| X7 | Low | a11y | `dropArea` is not keyboard-operable; toast lacks `aria-live`; modal lacks focus trap/restore. Add `tabindex`, `role="button"`, `aria-live="polite"`, and a one-line focus trap. |
| X8 | Low | `<meta>`/headers | Missing `charset=utf-8` (server should send `text/html; charset=utf-8`) and `Cache-Control: no-cache` on `/` — stale dashboards after firmware updates are a recurring support issue. |
| X9 | Low | /api/settings route | `order`, `fit`, `transition` clamp at NVS layer but not at the route — out-of-range values are stored then fall back unpredictably. Clamp in the handler before persisting. |

**X1 patch (client):**
```js
// replace innerHTML+onclick cards with:
const btn = document.createElement('button');
btn.className = 'card';
btn.dataset.name = p.name;          // raw text, not HTML/JS context
btn.textContent = p.name;           // textContent = safe by construction
btn.addEventListener('click', () => showPhotoNow(btn.dataset.name));
grid.appendChild(btn);
```

---

## 7. Upload System Review

**Verified OK (post-fix architecture):** PSRAM 6 MB buffer → single 64 KB-slice SD write under one mutex hold → flush/close/rollback under same hold; `uploadHandlerActive` guards the stall watchdog during the blocking write; interleaved-upload guard rejects a second connection while one is streaming; dashboard `uploadWithRetry` (3 attempts, 2 s backoff) succeeds after rejections; failed writes set `sdMounted=false` for loop remount; `sanitizeFilename` applied to both path and `forcePhotoName`.

**Findings:**

| # | Sev | Location | Issue & fix |
|---|-----|----------|-------------|
| U1 | **High** | final callback | Up to ~15–25 s of blocking work (6 MB @ 4 MHz SPI + `scanPhotos()`) inside an AsyncTCP callback — new connections/other requests stall the entire time, and this was the root of past corruption-class bugs. Fix: on `final`, only mark `uploadCommitPending=true` and return 200; perform the SD write **and** scan in the loop task. (Biggest remaining win.) |
| U2 | Medium | uploadBuf | Allocated once, **never freed** — a permanent 6 MB PSRAM reservation even when no card is present. Fix: free it 60 s after the last upload (`if (uploadBuf && !isUploading && millis()-lastUploadActivity > 60000) { heap_caps_free(uploadBuf); uploadBuf=nullptr; }`). |
| U3 | Medium | interleaved guard | After a **genuine** failure, the legit browser retry within the 20 s window is rejected (dead connection kept touching `lastUploadActivity`) — recovery only happens because multiple retries outlast the window. Cleaner: reject only the **first** `index==0` arrival while `isUploading`, and once `uploadRequestFailed` is set, accept a fresh `index==0` immediately (clears state). Add `if (uploadRequestFailed && index==0) { /* accept as fresh */ }` before the guard. |
| U4 | Low | client | On per-chunk terminal failure, add `if (uploadRequestFailed) return;` early-exit so the remaining queued XHR chunks of the dead attempt aren't sent (wasted seconds). |
| U5 | Low | final log | `bytes / 1024` when `bytes==0` (empty file) logs 0 KB and succeeds with an empty photo — reject `bytes==0` explicitly. |
| U6 | Low | misc | Missing HTTP Range support on `/photos/<name>` will matter only if the UI ever resumes; today's short files make it moot — note for the future. |

---

## 8. Slideshow Engine Review

**Verified OK:** `millis()` wraparound math; transition wrap-safe coordinates; `currentPhotoIndex` bounds; `forcePhotoName` honored when no transition active; pause via /api/control.

**Findings:**

| # | Sev | Location | Issue & fix |
|---|-----|----------|-------------|
| L1 | **High** | upload final + async paths → globals | `photoPlaylist`, `currentTransition`, `forcePhotoName`, and `tft` calls are mutated/made from the **AsyncTCP task** (upload final: `scanPhotos(); forcePhotoName = safeName;`) while the loop task is mid-`tickTransition` iterating the same vector/string → **use-after-free crash** when a delete/slide-change races an upload completion. Fix: async handlers only set `volatile bool rescanPending=true; char pendingPhoto[64];` and the loop consumes them between ticks (pending-flags pattern). This aligns with A1. |
| L2 | Medium | tickTransition during upload | `tickTransition` frozen while `isUploading` → a half-drawn transition sticks on screen for the entire (potentially blocked) final write. With U1 fixed (no long write in async) this resolves; until then, keep ticking the **drawing** of the active transition and only defer loading the next photo. |
| L3 | Medium | upload + transition active | `forcePhotoName` set while a transition is in flight is applied **after** the transition completes (M4) — actually acceptable behavior once L2 is fixed; ensure the pending-photo override survives the transition bookkeeping (test: upload during a fade). |
| L4 | Low | failed-decode text | Any non-JPEG/PNG/BMP failure prints "Progressive JPEG" — misleading. Print actual sniffed extension/bytes. |
| L5 | Low | decode performance | Photos > ~2 MB on 4 MHz SPI cost 3–8 s to display; consider 10 MHz SPI (pins support it; the official code uses it) after verifying signal integrity — ~2.5× faster loads. |

---

## 9. OTA Review

| # | Sev | Issue |
|---|-----|-------|
| O1 | **Critical** | As per B1: no OTA partitions → all OTA paths fail at `esp_ota_begin`. Fix = custom 2-slot CSV (§2 patch). |
| O2 | Medium | No HTTP-side OTA handler wired with auth (depends on M7 auth) and no progress feedback — the elaborated web-method `.bin` upload should stream via `Update.begin(...)`, report 400/500 with `Update.getErrorString()`, and never restart mid-write. |
| O3 | Low | OTA interrupted mid-flash: with otadata, a failed ota_1 write simply leaves ota_0 bootable — safe **once O1 lands**; today there is no second slot at all. |
| O4 | Low | `ArduinoOTA.end()` missing on WiFi mode flips (see W3). |

---

## 10. Memory & Resource Analysis

**Layout:** 8 MB PSRAM (framebuffer `cfg` uses PSRAM — verified OK), ~21 % internal RAM static, firmware ~1.05 MB of 4 MB.

**Findings:**

| # | Sev | Issue |
|---|-----|-------|
| M1 | Medium | `uploadBuf` permanent 6 MB reservation (fix: U2). |
| M2 | Medium | `String` churn in `/api/status`, `/api/photos`, settings handlers → heap fragmentation on a long-running device (weeks; buffers in internal RAM). Use `reserve()`, reuse a static `char[512]` with `snprintf`, and prefer split responses. |
| M3 | Medium | `/api/photos` with thousands of photos builds a giant String response → OOM risk + slow (overlaps S1). Paginate (`?offset=`&limit=`) or stream with `AsyncChunkedResponse`. |
| M4 | Medium | `/api/deleteall` + delete loops should free/release frontend caches client-side too; server-side the playlist vector isn't `shrink_to_fit`ed after clear (memory held until next scan). |
| M5 | Low | Stack sizing: loop task handoffs into deep decode paths (pngle row callbacks) approach default limits with 4K images — bump thread stack where decode runs, or switch decode to a dedicated 8 KB-stack render task. |
| M6 | Low | RGB framebuffer + LVGL-less design fine; no leaks found in blob URLs (`revokeObjectURL` paired), request objects, or vertex/DMA buffers. |

---

## 11. Concurrency & FreeRTOS Analysis

**Verified OK:** every mutex take has a paired give on all paths (grepped); pollSD/scanPhotos never nest mutex takes; upload watchdog guarded by `uploadHandlerActive`.

**Findings:**

| # | Sev | Issue |
|---|-----|-------|
| T1 | **Critical** | C1: `/api/view` + `/api/thumb` (`AsyncFileResponse`) read SD on the AsyncTCP task with **no mutex**, racing the loop task's SD use → bus corruption, hung `File.read()`s, corrupt responses. Fix: custom `AsyncStreamResponse` that takes `sdMutex` around each `read()` chunk, or pre-buffer small files into PSRAM before returning the response. |
| T2 | **Critical** | C2/C3 (upload globals): partially fixed by the current guard; remaining edge = legit-retry rejection (U3) and chained interleaving. |
| T3 | **High** | Shared-state mutation from async task (L1) — the pending-flags refactor is the systemic fix. |
| T4 | Medium | Mutex held across multi-second JPEG decode and possibly multi-tens-of-seconds SD writes means **everything else** (dashboard, health poll) freezes meanwhile. Longer term: move decoding to its own task with its own SD handle and a bus arbiter, or raise SPI to 10 MHz (L5) to shrink critical sections. |
| T5 | Low | `esp_task_wdt_deinit()`/re-init pattern is unreliable on core 3.x — switch to `esp_task_wdt_reconfigure()` when changing the WDT budget around long ops. |
| T6 | Low | `isalnum(c)` with `signed char` >127 chars is UB — cast: `isalnum((unsigned char)c)`. |

---

## 12. Performance Analysis

- Large PNGs (§8 D1) can take up to minutes to draw due to heavy per-pixel fillRect rendering.
- `/api/photos` O(N) file opens (S1) degrades linearly; 1,000 photos ≈ 15–30 s response.
- Dashboard polling adds negligible load thanks to 304 reuse (once X4 fixed).

---

## 13. Reliability & Recovery Analysis

**Verified OK:** SD remount seeding after failed uploads; boot recovery (`loadSettings` + screen flows); `consecutiveFailures` escape-to-unreadable-screen; power-loss during SD write → rollback-with-remount cleans partial file; power loss mid-commit of settings → NVS transactional.

**Gaps:**

| # | Sev | Issue |
|---|-----|-------|
| R1 | Medium | Settings write-back ordering: `loadSettings()` runs **after** `initDisplay()` → saved brightness is applied one frame late (visible flash of default brightness at boot). Call `loadSettings()` before `initDisplay()`. |
| R2 | Medium | DNS/portal not cleaned post-provision (W2). |
| R3 | Low | No global "safe state" watchdog fallback (if decode ever wedges outside WDT coverage) — add a 30 s "slideshow heartbeat" reboot fallback in the loop when no tick has occurred, only armed while `!isUploading`. |

---

## 14. Edge Case Checklist

| Scenario | Status | Notes |
|---|---|---|
| SD missing at boot | ✅ | SD error screen, retry loop |
| SD removed mid-slideshow | ✅ (S2 race noted) | pollSD detects, screen switches, recovers on reinsert |
| Corrupted image file | ✅ | skipped, `consecutiveFailures` → unreadable screen, self-recovers |
| Empty photo folder | ✅ | "No Photos" screen |
| Full SD card | ⚠️ | clean failure; remount may loop (S4) |
| Upload interrupted (browser close/refresh/refresh-mid-upload/network drop) | ✅ | final never arrives → stall watchdog; partial discarded; remount |
| Interleaved/concurrent uploads | ✅ | guard + client retry |
| SD contention with slideshow during upload | ✅ | PSRAM-buffered (residual: final-write block, U1) |
| WiFi disconnect / router down | ✅ | AP+STA background retry |
| Router unreachable at boot / AP fallback isolated | ✅ | setup AP with captive DNS |
| OTA interruption | ✅ once O1 fixed | otadata keeps old slot |
| Low memory (heap) | ✅ | uploads in PSRAM; watch M2/M3 |
| Thousands of photos | ⚠️ | /api/photos OOM/stall (S1/M3); gallery DOM (X3) |
| Very large images (>6 MB) | ✅ | rejected with Auto-Convert guidance |
| Malicious filename | ⚠️ | upload path safe; SD-side names → XSS (X1) |
| Power loss during file write | ✅ | poll integrity + rollbacks; NVS safe |
| Wrong uVM format (progressive JPEG etc.) | ✅ | browser converts before upload |

---

## 15. Critical Issues

### C1 — SD streaming endpoints race the SD bus
- **Sev:** Critical · **Loc:** main.cpp `/api/view` (~1245), `/api/thumb` (~1290)
- **Root cause:** `AsyncFileResponse` reads the File lazily on the AsyncTCP task; `sdMutex` is never taken.
- **Repro:** Open the gallery while the slideshow transitions (SD active on loop task); repeat quickly; watch for hung views/corrupted responses or SPI error logs.
- **Impact:** Bus corruption, hung reads, watchdog prints, broken images — the same class of failure already fixed in pollSD/upload.
- **Fix:** Custom response whose `read()` chunks take `sdMutex` (500 ms timeout), or preload into PSRAM (files are ≤6 MB, we have the RAM).
```cpp
class MutexFileResponse : public AsyncFileResponse {
  size_t read(uint8_t* buf, size_t len) override {
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(500)) != pdTRUE) return 0;
    size_t n = AsyncFileResponse::read(buf, len);
    xSemaphoreGive(sdMutex);
    return n;
  }
};
```

### C2 — OTA impossible (partition table)
- **Sev:** Critical · **Loc:** board json partitions
- Details and patch in §2 (B1).

### C3 — Stored XSS via SD-side filenames → inline JS
- **Sev:** Critical/High · **Loc:** index_html.h ~769–781, main.cpp:1194
- Details and patch in §6 (X1/X2). Server-side escaping is mandatory (clients can't trust data from files copied via PC).

---

## 16. Prioritised Fix Plan

**P0 — merge blockers (≤1 day):**
1. C2/B1: custom 2-slot OTA partition table + set `board_build.partitions`. (One-time reflash required to break out of `huge_app`.)
2. C1: mutex-gated or PSRAM-preloaded responses for `/api/view` and `/api/thumb`.
3. C3：server-side `jsonEscape()` in `/api/photos`; client: `data-name` + delegated listeners instead of inline onclick.

**P1 — this week:**
4. H1/U1: move upload final-write + `scanPhotos` out of the async callback into the loop via `uploadCommitPending`.
5. H2/L1: pending-flags for `photoPlaylist`/`forcePhotoName`/TFT from async contexts (same line of change as #4).
6. H3/R1: `loadSettings()` before `initDisplay()`.
7. H4/S1: `/api/photos` — directory listing only + `jsonEscape` + reserve; paginate.
8. H5/S2: wrap pollSD's unmounted probe in the mutex.
9. S3: `mkdir(PHOTO_DIR)` after every successful mount.

**P2 — next sprint:**
10. U2 uploadBuf free timer; U3 retry-acceptance for `uploadRequestFailed`; U5 reject empty file.
11. W1–W5 (restart-pending flag, AP/DNS shutdown, OTA.end symmetry, compile-time OTA password check, AP passcode).
12. S4 full-card remount cap + S5 thumb semantics.
13. X3–X9 frontend polish (thumb endpoint, drop cache-buster for media, offline backoff + badge, toast/a11y/headers, route clamping).
14. O2/O3 wiring the HTTP OTA endpoint properly; M2/M3 String/response hygiene; D1/D4 decoder work (row decoder for PNG, per-row mutex, top-down BMP fix, BI_BITFIELDS guard).
15. L5 (10 MHz SPI — test signal integrity), M5 stack sizing, B3/B4 build hygiene.

---

## 17. Production Readiness Score

| Domain | Weight | Score |
|---|---|---|
| Functionality & features | 20 | 18 |
| Stability (happy path) | 20 | 18 |
| Concurrency safety | 15 | 9 (streaming/decoder mutex gaps) |
| Security | 10 | 4 (no auth, XSS, open AP) |
| Resource & memory | 10 | 8 |
| Error recovery | 10 | 8 |
| Code quality & maintainability | 10 | 7 |
| Build/OTA/deployment | 5 | 3 (OTA dead) |

**Overall: 79 / 100.**

- **Now:** solid, safe for a home LAN appliance with trusted users; all daily-use features verified working.
- **After P0:** 88/100 — deployable to beta users including OTA.
- **After P1:** 93/100 — production-grade for a LAN appliance.
- Reaching 95+: require auth (M7) + the P2 decoder/platform hardening (D1/D4, T4, L5).
