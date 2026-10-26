#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <ArduinoOTA.h>
#include <esp_task_wdt.h>   // Task Watchdog API (not pulled in by Arduino.h)
#include <vector>
#include "config.h"
#include "index_html.h"
#include "image_decoder.h"

// ============================================================
// Global Instances
// ============================================================
TFT_eSPI tft = TFT_eSPI();
Preferences prefs;
AsyncWebServer server(HTTP_PORT);
DNSServer dnsServer;
SemaphoreHandle_t sdMutex = NULL;

// Hardware & System State
bool sdMounted = false;
bool wifiConnected = false;
bool wifiConnecting = false;
unsigned long wifiConnectStart = 0;
bool apMode = false;
bool otaReady = false;
String currentSSID = "";
String currentIP = "";

// Storage Stats (in Megabytes)
uint32_t sdTotalMB = 0;
uint32_t sdUsedMB = 0;
uint32_t sdFreeMB = 0;

// Slideshow Configuration & Runtime State
uint32_t slideIntervalSec = DEFAULT_INTERVAL_SEC;
uint8_t orderMode = DEFAULT_ORDER_MODE;         // 0: Sequential, 1: Shuffle
uint8_t fitMode = DEFAULT_FIT_MODE;             // 0: Letterbox, 1: Crop/Fill, 2: Auto
uint8_t transitionStyle = DEFAULT_TRANSITION;   // 0: Instant, 1-11: Effects, 12: Random
uint8_t screenBrightness = DEFAULT_BRIGHTNESS;  // 0–255 (255 = full)
bool isPaused = false;
volatile bool isUploading = false;
bool forceNextSlide = false;
String forcePhotoName = "";

std::vector<String> photoPlaylist;
int currentPhotoIndex = -1;
unsigned long lastSlideChangeTime = 0;
int consecutiveFailures = 0;

// Timing counters
unsigned long lastSDPoll = 0;
unsigned long lastWifiRetry = 0;
unsigned long lastSDAnim = 0;
unsigned long lastUploadActivity = 0;
int sdRetryCount = 0;
int wifiFailCount = 0;

// Upload transfer state (single upload at a time; the Web UI uploads sequentially)
File uploadFile;
bool uploadFileOk = false;
volatile bool uploadRequestFailed = false;

// Transition Animation State Machine (non-blocking)
struct TransitionAnim {
    TransitionStyle type = TRANS_INSTANT;
    int step = 0;
    int subStep = 0;
    unsigned long nextMs = 0;
    bool active = false;
    bool waitingForImage = false;
    FitMode fitMode = FIT_LETTERBOX;
    String filepath;
};
TransitionAnim currentTransition;

bool isTransitionActive() { return currentTransition.active; }
void startTransition(TransitionStyle trans, FitMode fit, const char* filepath);
void tickTransition();

// UI Dirty flags
enum ScreenState {
    SCREEN_BOOT,
    SCREEN_SD_ERROR,
    SCREEN_AP_SETUP,
    SCREEN_NO_PHOTOS,
    SCREEN_SLIDESHOW
};
ScreenState currentScreen = SCREEN_BOOT;

// ============================================================
// Forward Declarations
// ============================================================
void initDisplay();
void initSD();
void initWiFi();
void startAPMode();
void startSTAMode(const String& ssid, const String& pass);
void setupWebServer();
void loadSettings();
void saveSettingsToNVS();
void scanPhotos();
void advanceSlide(bool forward = true);
void showCurrentPhoto();
void drawBootScreen();
void drawSDErrorScreen();
void tickSDErrorScreen();
void drawAPScreen();
void drawNoPhotosScreen();
void drawUnreadablePhotoScreen(const char* filename);
void pollSD();
void handleWiFiReconnect();
String sanitizeFilename(const String& filename);
bool isPathSafe(const String& path);
bool isTransitionActive();
void startTransition(TransitionStyle trans, FitMode fit, const char* filepath);
void tickTransition();

// ============================================================
// Setup
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n==============================================");
    Serial.println("  Elecrow 2.8\" ESP32 Digital Photo Frame");
    Serial.println("==============================================");

    // Initialize watchdog timer (log only — panic=false so a busy transfer can never reboot)
    esp_task_wdt_init(WDT_TIMEOUT_MS / 1000, false);
    esp_task_wdt_add(NULL); // add current thread

    sdMutex = xSemaphoreCreateMutex();
    randomSeed(analogRead(34) ^ micros());

    pinMode(TFT_BL, OUTPUT);
    // Use LEDC PWM for backlight so brightness can be adjusted smoothly (0-255)
    ledcSetup(TFT_BL_LEDC_CH, TFT_BL_LEDC_FREQ, TFT_BL_LEDC_RES);
    ledcAttachPin(TFT_BL, TFT_BL_LEDC_CH);
    ledcWrite(TFT_BL_LEDC_CH, DEFAULT_BRIGHTNESS);

    initDisplay();
    drawBootScreen();

    loadSettings();
    initSD();
    initWiFi();
    setupWebServer();

    if (sdMounted) {
        scanPhotos();
        if (photoPlaylist.size() > 0) {
            advanceSlide(true);
        } else {
            currentScreen = SCREEN_NO_PHOTOS;
            drawNoPhotosScreen();
        }
    } else {
        currentScreen = SCREEN_SD_ERROR;
        drawSDErrorScreen();
    }

    Serial.println("[SYSTEM] Setup completed successfully!");
}

// ============================================================
// Loop
// ============================================================
void loop() {
    unsigned long now = millis();

    // Feed watchdog timer
    static unsigned long lastWdtFeed = 0;
    if (now - lastWdtFeed >= WDT_FEED_INTERVAL) {
        lastWdtFeed = now;
        esp_task_wdt_reset();
    }

    // 1. Handle AP Mode Captive Portal DNS
    if (apMode) {
        dnsServer.processNextRequest();
    }

    // 2. Handle OTA if enabled
    if (otaReady) {
        ArduinoOTA.handle();
    }

    // 3. SD Card Status Polling
    if (now - lastSDPoll >= SD_POLL_INTERVAL) {
        lastSDPoll = now;
        pollSD();
    }

    // 4. SD Card Error Screen Animation Tick
    if (!sdMounted && currentScreen == SCREEN_SD_ERROR && now - lastSDAnim >= SD_RETRY_ANIM_MS) {
        lastSDAnim = now;
        tickSDErrorScreen();
    }

    // 5. Non-blocking Transition Animation Tick
    tickTransition();

    // 5b. Self-healing: an upload that stalled (client vanished mid-transfer) would otherwise
    // keep its SD file handle open forever. Reclaim it after 20 s of no activity.
    if (isUploading && now - lastUploadActivity > 20000UL) {
        if (uploadFile) {
            if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                uploadFile.close();
                xSemaphoreGive(sdMutex);
            }
        }
        uploadFileOk = false;
        isUploading = false;
        Serial.println("[UPLOAD] Stalled upload recovered (client vanished)");
    }

    // 6. Non-blocking WiFi Reconnect Management
    if (!apMode && !wifiConnected) {
        if (WiFi.status() == WL_CONNECTED) {
            wifiConnected = true;
            wifiConnecting = false;
            wifiFailCount = 0;
            currentIP = WiFi.localIP().toString();
            Serial.printf("[WIFI] Connected! IP: %s\n", currentIP.c_str());
            if (sdMounted && photoPlaylist.size() > 0 && currentScreen != SCREEN_SLIDESHOW) {
                currentScreen = SCREEN_SLIDESHOW;
                showCurrentPhoto();
                lastSlideChangeTime = millis();
            }

            if (!otaReady) {
                ArduinoOTA.setHostname(WIFI_HOSTNAME);
                ArduinoOTA.begin();
                otaReady = true;
            }
        } else if (wifiConnecting && millis() - wifiConnectStart >= WIFI_CONNECT_TIMEOUT) {
            wifiConnecting = false;
            Serial.println("[WIFI] Connection timeout, falling back to SoftAP");
            startAPMode();
        } else if (!wifiConnecting && now - lastWifiRetry >= WIFI_RETRY_INTERVAL) {
            lastWifiRetry = now;
            handleWiFiReconnect();
        }
    } else if (!apMode && wifiConnected && WiFi.status() != WL_CONNECTED) {
        wifiConnected = false;
        otaReady = false;
        currentIP = "";
        Serial.println("[WIFI] Connection lost, will retry...");
    }

    // 6. Slideshow Engine
    if (sdMounted && photoPlaylist.size() > 0) {
        bool shouldChange = false;

        if (forcePhotoName.length() > 0) {
            // Find and show specific photo requested by user
            for (size_t i = 0; i < photoPlaylist.size(); i++) {
                if (photoPlaylist[i] == forcePhotoName) {
                    currentPhotoIndex = i;
                    break;
                }
            }
            forcePhotoName = "";
            consecutiveFailures = 0;
            shouldChange = true;
        } else if (forceNextSlide) {
            forceNextSlide = false;
            advanceSlide(true);
            consecutiveFailures = 0;
            shouldChange = true;
        } else if (!isPaused && !isUploading && !isTransitionActive()) {
            // If all photos currently fail, retry at a gentle 10s interval instead of tight loop
            uint32_t interval = (consecutiveFailures >= (int)photoPlaylist.size())
                                ? 10000UL
                                : (slideIntervalSec * 1000UL);

            if (now - lastSlideChangeTime >= interval) {
                if (consecutiveFailures < (int)photoPlaylist.size()) {
                    advanceSlide(true);
                }
                shouldChange = true;
            }
        }

        if (shouldChange && !isTransitionActive()) {
            currentScreen = SCREEN_SLIDESHOW;
            showCurrentPhoto();
            lastSlideChangeTime = millis();
        }
    } else if (sdMounted && photoPlaylist.size() == 0 && currentScreen != SCREEN_NO_PHOTOS) {
        currentScreen = SCREEN_NO_PHOTOS;
        drawNoPhotosScreen();
    }

    delay(10);
}

// ============================================================
// Display Initialization & System Screens
// ============================================================
void initDisplay() {
    Serial.println("[DISPLAY] Initializing TFT_eSPI...");
    tft.init();
    tft.setRotation(1); // 320x240 landscape
    tft.fillScreen(TFT_BLACK);
}

void drawBootScreen() {
    // A high-contrast, compact welcome screen sized for the native 320x240 panel.
    tft.fillScreen(0x0841);
    tft.fillRect(0, 0, SCREEN_WIDTH, 6, 0x07FF);
    tft.fillRoundRect(18, 24, SCREEN_WIDTH - 36, 190, 12, 0x10A2);
    tft.drawRoundRect(18, 24, SCREEN_WIDTH - 36, 190, 12, 0x2965);

    tft.fillCircle(SCREEN_WIDTH / 2, 65, 23, 0x01E8);
    tft.drawCircle(SCREEN_WIDTH / 2, 65, 23, 0x07FF);
    tft.drawCircle(SCREEN_WIDTH / 2, 65, 16, TFT_WHITE);
    tft.fillRect(SCREEN_WIDTH / 2 - 10, 60, 20, 13, 0x01E8);
    tft.drawRect(SCREEN_WIDTH / 2 - 10, 60, 20, 13, TFT_WHITE);
    tft.drawFastVLine(SCREEN_WIDTH / 2 - 4, 56, 4, TFT_WHITE);
    tft.drawFastVLine(SCREEN_WIDTH / 2 + 4, 56, 4, TFT_WHITE);

    tft.setTextColor(TFT_WHITE, 0x10A2);
    tft.drawCentreString("DIGITAL PHOTO FRAME", SCREEN_WIDTH / 2, 100, 2);
    tft.setTextColor(0x07FF, 0x10A2);
    tft.drawCentreString("BY FUSION AUTOMATE", SCREEN_WIDTH / 2, 130, 2);

    tft.fillRoundRect(38, 166, SCREEN_WIDTH - 76, 30, 7, 0x18E3);
    tft.setTextColor(TFT_WHITE, 0x18E3);
    tft.drawCentreString("Starting up...", SCREEN_WIDTH / 2, 174, 2);

    tft.setTextColor(0x9CF3, 0x0841);
    tft.drawCentreString("POWERED BY FUSION AUTOMATE", SCREEN_WIDTH / 2, 222, 1);
}

void drawSDErrorScreen() {
    tft.fillScreen(0x1800); // Deep dark red

    // Alert Banner
    tft.fillRect(0, 0, SCREEN_WIDTH, 48, 0x8800);
    tft.setTextColor(TFT_WHITE, 0x8800);
    tft.drawCentreString("! SD CARD ERROR !", SCREEN_WIDTH / 2, 12, 2);

    // Card Box
    tft.fillRoundRect(20, 65, SCREEN_WIDTH - 40, 120, 8, 0x2800);
    tft.drawRoundRect(20, 65, SCREEN_WIDTH - 40, 120, 8, 0xF800);

    tft.setTextColor(0xFD20, 0x2800); // Orange
    tft.drawCentreString("MicroSD Card Not Detected", SCREEN_WIDTH / 2, 80, 2);

    tft.setTextColor(TFT_WHITE, 0x2800);
    tft.drawCentreString("1. Insert FAT32 formatted card", SCREEN_WIDTH / 2, 110, 1);
    tft.drawCentreString("2. Copy photos into /photos folder", SCREEN_WIDTH / 2, 130, 1);

    tickSDErrorScreen();
}

void tickSDErrorScreen() {
    static bool blink = false;
    blink = !blink;

    tft.fillRect(30, 155, SCREEN_WIDTH - 60, 22, 0x2800);
    tft.setTextColor(blink ? 0x07FF : 0x9CF3, 0x2800);
    char buf[48];
    snprintf(buf, sizeof(buf), "Auto-retrying mount (attempt #%d)...", sdRetryCount);
    tft.drawCentreString(buf, SCREEN_WIDTH / 2, 158, 1);
}

void drawAPScreen() {
    tft.fillScreen(0x0841);
    tft.fillRect(0, 0, SCREEN_WIDTH, 6, 0x07FF);
    tft.setTextColor(TFT_WHITE, 0x0841);
    tft.drawCentreString("WI-FI SETUP", SCREEN_WIDTH / 2, 15, 4);
    tft.setTextColor(0x9CF3, 0x0841);
    tft.drawCentreString("Connect your phone to this network", SCREEN_WIDTH / 2, 50, 1);

    tft.fillRoundRect(16, 72, SCREEN_WIDTH - 32, 126, 10, 0x10A2);
    tft.drawRoundRect(16, 72, SCREEN_WIDTH - 32, 126, 10, 0x07FF);
    tft.setTextColor(0x07FF, 0x10A2);
    tft.drawCentreString("NETWORK NAME", SCREEN_WIDTH / 2, 84, 1);
    tft.setTextColor(TFT_YELLOW, 0x10A2);
    tft.drawCentreString(AP_SSID, SCREEN_WIDTH / 2, 101, 4);
    tft.drawFastHLine(38, 135, SCREEN_WIDTH - 76, 0x2965);
    tft.setTextColor(0x9CF3, 0x10A2);
    tft.drawCentreString("THEN OPEN IN YOUR BROWSER", SCREEN_WIDTH / 2, 146, 1);
    tft.setTextColor(TFT_WHITE, 0x10A2);
    tft.drawCentreString("192.168.4.1", SCREEN_WIDTH / 2, 163, 4);

    tft.setTextColor(0x9CF3, 0x0841);
    tft.drawCentreString("Setup page opens automatically on many phones", SCREEN_WIDTH / 2, 218, 1);
}

void drawNoPhotosScreen() {
    tft.fillScreen(0x0841);

    tft.fillRect(0, 0, SCREEN_WIDTH, 44, 0x18E3);
    tft.setTextColor(0x3DFE, 0x18E3);
    tft.drawCentreString("NO PHOTOS ON CARD", SCREEN_WIDTH / 2, 10, 2);

    tft.fillRoundRect(20, 60, SCREEN_WIDTH - 40, 130, 8, 0x10A2);
    tft.drawRoundRect(20, 60, SCREEN_WIDTH - 40, 130, 8, 0x2965);

    tft.setTextColor(TFT_WHITE, 0x10A2);
    tft.drawCentreString("SD card is mounted and ready!", SCREEN_WIDTH / 2, 74, 1);

    tft.setTextColor(0x07FF, 0x10A2);
    tft.drawCentreString("Upload photos via browser:", SCREEN_WIDTH / 2, 102, 1);

    tft.setTextColor(TFT_GREEN, 0x10A2);
    String url = "http://" + (wifiConnected ? currentIP : String("192.168.4.1"));
    tft.drawCentreString(url.c_str(), SCREEN_WIDTH / 2, 124, 2);

    tft.setTextColor(0x9CF3, 0x10A2);
    tft.drawCentreString("Supported: JPEG, PNG, BMP", SCREEN_WIDTH / 2, 158, 1);

    tft.setTextColor(0x52AA, 0x0841);
    tft.drawCentreString("Drag and drop images onto the web page", SCREEN_WIDTH / 2, 214, 1);
}

void drawUnreadablePhotoScreen(const char* filename) {
    currentScreen = SCREEN_SLIDESHOW;
    tft.fillScreen(0x18C3); // Slate dark

    // Top warning bar
    tft.fillRect(0, 0, SCREEN_WIDTH, 40, 0xD260); // Warm amber / orange
    tft.setTextColor(TFT_WHITE, 0xD260);
    tft.setTextSize(2);
    tft.drawCentreString("UNSUPPORTED PHOTO", SCREEN_WIDTH / 2, 6, 2);

    // Filename
    tft.setTextColor(0xFD20, 0x18C3); // Gold
    tft.setTextSize(1);
    tft.drawCentreString(filename, SCREEN_WIDTH / 2, 54, 2);

    // Explanation
    tft.setTextColor(TFT_WHITE, 0x18C3);
    tft.drawCentreString("Format unsupported (Progressive JPEG)", SCREEN_WIDTH / 2, 82, 2);
    tft.setTextColor(0x9CF3, 0x18C3);
    tft.drawCentreString("Hardware decoder requires Baseline JPEG.", SCREEN_WIDTH / 2, 104, 1);

    // Web instruction box
    tft.fillRoundRect(16, 126, SCREEN_WIDTH - 32, 94, 8, 0x10A2);
    tft.drawRoundRect(16, 126, SCREEN_WIDTH - 32, 94, 8, 0x3DFE);

    tft.setTextColor(0x3DFE, 0x10A2);
    tft.drawCentreString("Upload photos via Web Dashboard to auto-convert:", SCREEN_WIDTH / 2, 136, 1);

    tft.setTextColor(TFT_GREENYELLOW, 0x10A2);
    String ipStr = currentIP.length() > 0 ? ("http://" + currentIP) : "http://192.168.4.1";
    tft.drawCentreString(ipStr.c_str(), SCREEN_WIDTH / 2, 156, 4);

    tft.setTextColor(TFT_LIGHTGREY, 0x10A2);
    tft.drawCentreString("Web UI auto-converts WhatsApp/phone photos!", SCREEN_WIDTH / 2, 196, 1);
}

// ============================================================
// SD Card Mounting & Polling
// ============================================================
void initSD() {
    Serial.println("[SD] Initializing SD card on VSPI (Pins: SCK=18, MISO=19, MOSI=23, CS=5)...");
    // Some cards power up slowly relative to the ESP32. Retry briefly at boot,
    // then let the normal background poll continue retrying without blocking WiFi.
    for (int attempt = 0; attempt < 3 && !sdMounted; ++attempt) {
        SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
        if (SD.begin(SD_CS, SPI, SD_SPI_SPEED)) {
            sdMounted = true;
            break;
        }
        SD.end();
        if (attempt < 2) delay(250);
    }

    if (sdMounted) {
        sdMounted = true;
        sdRetryCount = 0;

        uint64_t totalBytes = SD.totalBytes();
        uint64_t usedBytes = SD.usedBytes();
        sdTotalMB = (uint32_t)(totalBytes / (1024ULL * 1024ULL));
        sdUsedMB = (uint32_t)(usedBytes / (1024ULL * 1024ULL));
        sdFreeMB = (sdUsedMB <= sdTotalMB) ? (sdTotalMB - sdUsedMB) : 0;

        Serial.printf("[SD] Mounted successfully! Total: %u MB, Used: %u MB, Free: %u MB\n",
                      sdTotalMB, sdUsedMB, sdFreeMB);

        // Ensure /photos directory exists
        if (!SD.exists(PHOTO_DIR)) {
            SD.mkdir(PHOTO_DIR);
            Serial.printf("[SD] Created folder: %s\n", PHOTO_DIR);
        }
    } else {
        sdMounted = false;
        sdTotalMB = 0;
        sdUsedMB = 0;
        sdFreeMB = 0;
        Serial.println("[SD] Mount FAILED - will retry automatically");
    }
}

void pollSD() {
    bool wasMounted = sdMounted;

    if (!sdMounted) {
        sdRetryCount++;
        SD.end();
        SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

        if (SD.begin(SD_CS, SPI, SD_SPI_SPEED)) {
            sdMounted = true;
            sdRetryCount = 0;
            uint64_t totalBytes = SD.totalBytes();
            uint64_t usedBytes = SD.usedBytes();
            sdTotalMB = (uint32_t)(totalBytes / (1024ULL * 1024ULL));
            sdUsedMB = (uint32_t)(usedBytes / (1024ULL * 1024ULL));
            sdFreeMB = (sdUsedMB <= sdTotalMB) ? (sdTotalMB - sdUsedMB) : 0;
            Serial.printf("[SD] Re-mounted OK! Total: %u MB, Free: %u MB\n", sdTotalMB, sdFreeMB);

            if (!SD.exists(PHOTO_DIR)) {
                SD.mkdir(PHOTO_DIR);
            }

            scanPhotos();
            if (photoPlaylist.size() > 0) {
                advanceSlide(true);
            } else {
                currentScreen = SCREEN_NO_PHOTOS;
                drawNoPhotosScreen();
            }
        }
    } else {
        // Verify card still alive
        uint64_t total = SD.totalBytes();
        if (total == 0) {
            sdMounted = false;
            SD.end();
            sdTotalMB = 0;
            sdUsedMB = 0;
            sdFreeMB = 0;
            Serial.println("[SD] Card removed or read fault!");
            currentScreen = SCREEN_SD_ERROR;
            drawSDErrorScreen();
        } else {
            uint64_t used = SD.usedBytes();
            sdTotalMB = (uint32_t)(total / (1024ULL * 1024ULL));
            sdUsedMB = (uint32_t)(used / (1024ULL * 1024ULL));
            sdFreeMB = (sdUsedMB <= sdTotalMB) ? (sdTotalMB - sdUsedMB) : 0;
        }
    }
}

// ============================================================
// Playlist & Slideshow Logic
// ============================================================
void scanPhotos() {
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1500)) != pdTRUE) return;

    photoPlaylist.clear();
    consecutiveFailures = 0;
    File dir = SD.open(PHOTO_DIR);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        xSemaphoreGive(sdMutex);
        return;
    }

    File entry = dir.openNextFile();
    while (entry && photoPlaylist.size() < MAX_PHOTOS_PLAYLIST) {
        if (!entry.isDirectory()) {
            String fname = String(entry.name());
            String lower = fname;
            lower.toLowerCase();

            if ((lower.endsWith(".jpg") || lower.endsWith(".jpeg") ||
                lower.endsWith(".png") || lower.endsWith(".bmp")) &&
                !lower.endsWith(".thumb.jpg")) {
                // If fname starts with "/photos/", strip it or store clean filename
                if (fname.startsWith(PHOTO_DIR "/")) {
                    fname = fname.substring(strlen(PHOTO_DIR) + 1);
                } else if (fname.startsWith("/")) {
                    fname = fname.substring(1);
                }
                photoPlaylist.push_back(fname);
            }
        }
        entry.close();
        entry = dir.openNextFile();
    }

    if (photoPlaylist.size() >= MAX_PHOTOS_PLAYLIST) {
        Serial.printf("[PLAYLIST] WARNING: Playlist limit reached (%d). Remaining files ignored.\n", MAX_PHOTOS_PLAYLIST);
    }
    dir.close();
    xSemaphoreGive(sdMutex);

    Serial.printf("[PLAYLIST] Discovered %u photos in %s\n", photoPlaylist.size(), PHOTO_DIR);
}

void advanceSlide(bool forward) {
    if (photoPlaylist.empty()) {
        currentPhotoIndex = -1;
        return;
    }

    if (orderMode == 1) { // Shuffle / Random
        if (photoPlaylist.size() == 1) {
            currentPhotoIndex = 0;
        } else {
            int nextIdx = currentPhotoIndex;
            while (nextIdx == currentPhotoIndex) {
                nextIdx = random(0, photoPlaylist.size());
            }
            currentPhotoIndex = nextIdx;
        }
    } else { // Sequential
        if (forward) {
            currentPhotoIndex = (currentPhotoIndex + 1) % photoPlaylist.size();
        } else {
            currentPhotoIndex = (currentPhotoIndex - 1 + photoPlaylist.size()) % photoPlaylist.size();
        }
    }
}

void showCurrentPhoto() {
    if (currentPhotoIndex < 0 || currentPhotoIndex >= (int)photoPlaylist.size()) {
        return;
    }

    // Briefly show connection instructions before the first slideshow image.
    static bool introShown = false;
    if (!introShown) {
        introShown = true;
        tft.fillScreen(0x0841);
        tft.fillRect(0, 0, SCREEN_WIDTH, 6, 0x07FF);
        tft.setTextColor(TFT_WHITE, 0x0841);
        tft.drawCentreString("FRAME READY", SCREEN_WIDTH / 2, 16, 4);
        tft.setTextColor(0x9CF3, 0x0841);
        tft.drawCentreString("OPEN THE PHOTO DASHBOARD", SCREEN_WIDTH / 2, 53, 1);

        tft.fillRoundRect(20, 76, SCREEN_WIDTH - 40, 86, 10, 0x10A2);
        tft.drawRoundRect(20, 76, SCREEN_WIDTH - 40, 86, 10, 0x07FF);
        tft.setTextColor(0x07FF, 0x10A2);
        tft.drawCentreString("ADDRESS", SCREEN_WIDTH / 2, 85, 1);
        String url = "http://" + (apMode ? WiFi.softAPIP().toString() : currentIP);
        if (url == "http://") url = "CONNECT TO WI-FI";
        tft.setTextColor(TFT_YELLOW, 0x10A2);
        tft.drawCentreString(url.c_str(), SCREEN_WIDTH / 2, 108, url.length() > 18 ? 2 : 4);

        tft.setTextColor(TFT_WHITE, 0x0841);
        tft.drawCentreString("Use a phone or computer on the same Wi-Fi", SCREEN_WIDTH / 2, 177, 1);
        tft.setTextColor(0x07FF, 0x0841);
        tft.drawCentreString("DIGITAL PHOTO FRAME  |  FUSION AUTOMATE", SCREEN_WIDTH / 2, 202, 1);
        tft.setTextColor(0x9CF3, 0x0841);
        tft.drawCentreString("Slideshow starting...", SCREEN_WIDTH / 2, 220, 1);
        delay(5000);
    }

    String filename = photoPlaylist[currentPhotoIndex];
    String fullPath = String(PHOTO_DIR) + "/" + filename;

    Serial.printf("[SLIDESHOW] Displaying [%d/%d]: %s\n",
                  currentPhotoIndex + 1, photoPlaylist.size(), fullPath.c_str());

    // Start non-blocking transition + render
    startTransition((TransitionStyle)transitionStyle, (FitMode)fitMode, fullPath.c_str());
}

// ============================================================
// Non-blocking Transition State Machine
// ============================================================
void startTransition(TransitionStyle trans, FitMode fit, const char* filepath) {
    if (trans == TRANS_INSTANT) {
        // No transition - render immediately
        currentTransition.active = false;
        currentTransition.waitingForImage = true;
        currentTransition.filepath = filepath;
        currentTransition.fitMode = fit;
        return;
    }

    // Pick random effect if AUTO
    TransitionStyle effectiveTrans = trans;
    if (trans == TRANS_RANDOM) {
        effectiveTrans = (TransitionStyle)random(1, 12);
    }

    currentTransition.type = effectiveTrans;
    currentTransition.step = 0;
    currentTransition.subStep = 0;
    currentTransition.nextMs = millis();
    currentTransition.active = true;
    currentTransition.waitingForImage = false;
    currentTransition.fitMode = fit;
    currentTransition.filepath = filepath;

    Serial.printf("[TRANS] Starting effect %d for %s\n", effectiveTrans, filepath);
}

void tickTransition() {
    if (!currentTransition.active) {
        if (currentTransition.waitingForImage) {
            // Render the image now
            bool ok = renderImageFile(currentTransition.filepath.c_str(), currentTransition.fitMode, TRANS_INSTANT);
            currentTransition.waitingForImage = false;
            if (!ok) {
                consecutiveFailures++;
                Serial.printf("[SLIDESHOW] Corrupted or unsupported image: %s (fail count: %d)\n",
                              currentTransition.filepath.c_str(), consecutiveFailures);
                if (consecutiveFailures >= (int)photoPlaylist.size()) {
                    Serial.printf("[SLIDESHOW] All %d photo(s) in playlist failed to render. Displaying error notice.\n",
                                  (int)photoPlaylist.size());
                    drawUnreadablePhotoScreen(currentTransition.filepath.c_str());
                } else {
                    advanceSlide(true);
                    showCurrentPhoto();
                }
            } else {
                consecutiveFailures = 0;
            }
        }
        return;
    }

    unsigned long now = millis();
    if (now < currentTransition.nextMs) return;

    TransitionStyle t = currentTransition.type;
    int step = currentTransition.step;
    int subStep = currentTransition.subStep;

    bool done = false;

    switch (t) {
        case TRANS_WIPE_RIGHT:
            if (step * TRANS_WIPE_STEP < SCREEN_WIDTH) {
                tft.fillRect(step * TRANS_WIPE_STEP, 0, TRANS_WIPE_STEP, SCREEN_HEIGHT, TFT_BLACK);
                currentTransition.step++;
                currentTransition.nextMs = now + TRANS_WIPE_DELAY_MS;
            } else done = true;
            break;

        case TRANS_WIPE_LEFT:
            if (SCREEN_WIDTH - (step + 1) * TRANS_WIPE_STEP >= 0) {
                tft.fillRect(SCREEN_WIDTH - (step + 1) * TRANS_WIPE_STEP, 0, TRANS_WIPE_STEP, SCREEN_HEIGHT, TFT_BLACK);
                currentTransition.step++;
                currentTransition.nextMs = now + TRANS_WIPE_DELAY_MS;
            } else done = true;
            break;

        case TRANS_WIPE_DOWN:
            if (step * TRANS_WIPE_STEP < SCREEN_HEIGHT) {
                tft.fillRect(0, step * TRANS_WIPE_STEP, SCREEN_WIDTH, TRANS_WIPE_STEP, TFT_BLACK);
                currentTransition.step++;
                currentTransition.nextMs = now + TRANS_WIPE_DELAY_MS;
            } else done = true;
            break;

        case TRANS_WIPE_UP:
            if (SCREEN_HEIGHT - (step + 1) * TRANS_WIPE_STEP >= 0) {
                tft.fillRect(0, SCREEN_HEIGHT - (step + 1) * TRANS_WIPE_STEP, SCREEN_WIDTH, TRANS_WIPE_STEP, TFT_BLACK);
                currentTransition.step++;
                currentTransition.nextMs = now + TRANS_WIPE_DELAY_MS;
            } else done = true;
            break;

        case TRANS_BLINDS_VERT:
            if (step < TRANS_BLINDS_STEPS) {
                for (int blind = 0; blind < 16; blind++) {
                    int bx = blind * 20 + step;
                    if (bx < SCREEN_WIDTH) tft.drawFastVLine(bx, 0, SCREEN_HEIGHT, TFT_BLACK);
                }
                currentTransition.step++;
                currentTransition.nextMs = now + TRANS_BLINDS_DELAY_MS;
            } else done = true;
            break;

        case TRANS_BLINDS_HORIZ:
            if (step < TRANS_BLINDS_STEPS) {
                for (int blind = 0; blind < 12; blind++) {
                    int by = blind * 20 + step;
                    if (by < SCREEN_HEIGHT) tft.drawFastHLine(0, by, SCREEN_WIDTH, TFT_BLACK);
                }
                currentTransition.step++;
                currentTransition.nextMs = now + TRANS_BLINDS_DELAY_MS;
            } else done = true;
            break;

        case TRANS_CURTAIN_OPEN: {
            int midX = SCREEN_WIDTH / 2;
            if (step * TRANS_CURTAIN_STEP <= midX) {
                int w = step * TRANS_CURTAIN_STEP;
                tft.fillRect(midX - w, 0, TRANS_CURTAIN_STEP, SCREEN_HEIGHT, TFT_BLACK);
                tft.fillRect(midX + w - TRANS_CURTAIN_STEP, 0, TRANS_CURTAIN_STEP, SCREEN_HEIGHT, TFT_BLACK);
                currentTransition.step++;
                currentTransition.nextMs = now + TRANS_CURTAIN_DELAY_MS;
            } else done = true;
            break;
        }

        case TRANS_CURTAIN_CLOSE: {
            int halfW = SCREEN_WIDTH / 2;
            if (step * TRANS_CURTAIN_STEP <= halfW) {
                int w = step * TRANS_CURTAIN_STEP;
                tft.fillRect(w, 0, TRANS_CURTAIN_STEP, SCREEN_HEIGHT, TFT_BLACK);
                tft.fillRect(SCREEN_WIDTH - w - TRANS_CURTAIN_STEP, 0, TRANS_CURTAIN_STEP, SCREEN_HEIGHT, TFT_BLACK);
                currentTransition.step++;
                currentTransition.nextMs = now + TRANS_CURTAIN_DELAY_MS;
            } else done = true;
            break;
        }

        case TRANS_BOX_IN: {
            if (step <= TRANS_BOX_STEPS) {
                int w = (SCREEN_WIDTH * step) / (TRANS_BOX_STEPS * 2);
                int h = (SCREEN_HEIGHT * step) / (TRANS_BOX_STEPS * 2);
                tft.fillRect(0, 0, SCREEN_WIDTH, h, TFT_BLACK);
                tft.fillRect(0, SCREEN_HEIGHT - h, SCREEN_WIDTH, h, TFT_BLACK);
                tft.fillRect(0, 0, w, SCREEN_HEIGHT, TFT_BLACK);
                tft.fillRect(SCREEN_WIDTH - w, 0, w, SCREEN_HEIGHT, TFT_BLACK);
                currentTransition.step += TRANS_BOX_STEP_INC;
                currentTransition.nextMs = now + TRANS_BOX_DELAY_MS;
            } else {
                tft.fillScreen(TFT_BLACK);
                done = true;
            }
            break;
        }

        case TRANS_CHECKERBOARD: {
            if (subStep == 0) {
                // First pass - even squares
                if (step < SCREEN_HEIGHT) {
                    for (int bx = 0; bx < SCREEN_WIDTH; bx += TRANS_CHECKER_SIZE) {
                        if (((bx / TRANS_CHECKER_SIZE) + (step / TRANS_CHECKER_SIZE)) % 2 == 0) {
                            int cw = min(TRANS_CHECKER_SIZE, SCREEN_WIDTH - bx);
                            int ch = min(TRANS_CHECKER_SIZE, SCREEN_HEIGHT - step);
                            tft.fillRect(bx, step, cw, ch, TFT_BLACK);
                        }
                    }
                    currentTransition.step += TRANS_CHECKER_SIZE;
                    currentTransition.nextMs = now + TRANS_CHECKER_DELAY_MS;
                } else {
                    currentTransition.step = 0;
                    currentTransition.subStep = 1;
                    currentTransition.nextMs = now + TRANS_CHECKER_PAUSE_MS;
                }
            } else {
                // Second pass - odd squares
                if (step < SCREEN_HEIGHT) {
                    for (int bx = 0; bx < SCREEN_WIDTH; bx += TRANS_CHECKER_SIZE) {
                        if (((bx / TRANS_CHECKER_SIZE) + (step / TRANS_CHECKER_SIZE)) % 2 != 0) {
                            int cw = min(TRANS_CHECKER_SIZE, SCREEN_WIDTH - bx);
                            int ch = min(TRANS_CHECKER_SIZE, SCREEN_HEIGHT - step);
                            tft.fillRect(bx, step, cw, ch, TFT_BLACK);
                        }
                    }
                    currentTransition.step += TRANS_CHECKER_SIZE;
                    currentTransition.nextMs = now + TRANS_CHECKER_DELAY_MS;
                } else done = true;
            }
            break;
        }

        case TRANS_FADE: {
            if (step < TRANS_FADE_PASSES) {
                for (int y = step; y < SCREEN_HEIGHT; y += 4) {
                    tft.drawFastHLine(0, y, SCREEN_WIDTH, TFT_BLACK);
                }
                currentTransition.step++;
                currentTransition.nextMs = now + TRANS_FADE_DELAY_MS;
            } else done = true;
            break;
        }

        default:
            done = true;
            break;
    }

    if (done) {
        currentTransition.active = false;
        currentTransition.waitingForImage = true;
        currentTransition.nextMs = now;
    }
}

// ============================================================
// WiFi & Network Management
// ============================================================
void initWiFi() {
    prefs.begin(PREF_WIFI_NS, true);
    String savedSSID = prefs.getString(PREF_KEY_SSID, "");
    String savedPass = prefs.getString(PREF_KEY_PASS, "");
    prefs.end();

    WiFi.setHostname(WIFI_HOSTNAME);
    WiFi.setAutoReconnect(false);

    if (savedSSID.length() > 0) {
        Serial.printf("[WIFI] Connecting to saved WiFi SSID: '%s'...\n", savedSSID.c_str());
        startSTAMode(savedSSID, savedPass);
    } else if (HAS_DEFAULT_CREDENTIALS && strlen(DEFAULT_SSID) > 0) {
        Serial.printf("[WIFI] Using default credentials SSID: '%s'...\n", DEFAULT_SSID);
        startSTAMode(String(DEFAULT_SSID), String(DEFAULT_PASS));
    } else {
        Serial.println("[WIFI] No WiFi credentials found, starting SoftAP setup portal");
        startAPMode();
    }
}

void startSTAMode(const String& ssid, const String& pass) {
    apMode = false;
    currentSSID = ssid;
    wifiConnecting = true;
    wifiConnectStart = millis();

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.length() > 0 ? pass.c_str() : NULL);
    Serial.printf("[WIFI] STA connection initiated to '%s' (timeout %ds)\n",
                  ssid.c_str(), WIFI_CONNECT_TIMEOUT / 1000);
}

void startAPMode() {
    apMode = true;
    wifiConnected = false;
    wifiConnecting = false;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);

    delay(200);
    IPAddress apIP = WiFi.softAPIP();
    currentIP = apIP.toString();
    currentSSID = AP_SSID;

    // Start Captive Portal DNS on port 53
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(53, "*", apIP);

    Serial.printf("[WIFI] SoftAP Active! SSID: '%s', IP: %s\n", AP_SSID, currentIP.c_str());

    if (!sdMounted || photoPlaylist.empty()) {
        currentScreen = SCREEN_AP_SETUP;
        drawAPScreen();
    }
}

void handleWiFiReconnect() {
    wifiFailCount++;
    if (wifiFailCount >= MAX_WIFI_FAIL) {
        Serial.printf("[WIFI] Exceeded max retries (%d), falling back to AP mode\n", MAX_WIFI_FAIL);
        startAPMode();
        return;
    }

    prefs.begin(PREF_WIFI_NS, true);
    String savedSSID = prefs.getString(PREF_KEY_SSID, "");
    String savedPass = prefs.getString(PREF_KEY_PASS, "");
    prefs.end();

    if (savedSSID.length() > 0) {
        Serial.printf("[WIFI] Reconnect attempt %d/%d to '%s'...\n",
                      wifiFailCount, MAX_WIFI_FAIL, savedSSID.c_str());
        WiFi.disconnect();
        WiFi.begin(savedSSID.c_str(), savedPass.length() > 0 ? savedPass.c_str() : NULL);
    }
}

// ============================================================
// NVS Configuration Load/Save
// ============================================================
void loadSettings() {
    prefs.begin(PREF_SETTINGS_NS, true);
    slideIntervalSec = prefs.getUInt(PREF_KEY_INTERVAL, DEFAULT_INTERVAL_SEC);
    orderMode        = prefs.getUChar(PREF_KEY_ORDER, DEFAULT_ORDER_MODE);
    fitMode          = prefs.getUChar(PREF_KEY_FIT, DEFAULT_FIT_MODE);
    transitionStyle  = prefs.getUChar(PREF_KEY_TRANSITION, DEFAULT_TRANSITION);
    isPaused         = prefs.getUChar(PREF_KEY_PAUSED, 0) == 1;
    prefs.end();

    if (slideIntervalSec < 1) slideIntervalSec = DEFAULT_INTERVAL_SEC;
    if (transitionStyle > 12) transitionStyle = DEFAULT_TRANSITION;

    Serial.printf("[CONFIG] Loaded settings: Interval=%us, Order=%d, Fit=%d, Trans=%d, Paused=%d\n",
                  slideIntervalSec, orderMode, fitMode, transitionStyle, isPaused);
}

void saveSettingsToNVS() {
    prefs.begin(PREF_SETTINGS_NS, false);
    prefs.putUInt(PREF_KEY_INTERVAL, slideIntervalSec);
    prefs.putUChar(PREF_KEY_ORDER, orderMode);
    prefs.putUChar(PREF_KEY_FIT, fitMode);
    prefs.putUChar(PREF_KEY_TRANSITION, transitionStyle);
    prefs.putUChar(PREF_KEY_PAUSED, isPaused ? 1 : 0);
    prefs.end();
    Serial.println("[CONFIG] Saved settings to NVS");
}

// ============================================================
// Web Server Route Handlers
// ============================================================
void setupWebServer() {
    // 1. Root: Serves dashboard HTML
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send_P(200, "text/html", INDEX_HTML);
    });

    // Captive Portal detection redirects
    server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/");
    });
    server.on("/canonical.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/");
    });
    server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/");
    });

    // 2. Status API: JSON
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* request) {
        String json = "{";
        json += "\"ip\":\"" + currentIP + "\",";
        json += "\"ssid\":\"" + currentSSID + "\",";
        json += "\"ap_mode\":" + String(apMode ? "true" : "false") + ",";
        json += "\"uptime\":" + String(millis() / 1000) + ",";
        json += "\"sd_mounted\":" + String(sdMounted ? "true" : "false") + ",";
        json += "\"sd_total_mb\":" + String(sdTotalMB) + ",";
        json += "\"sd_used_mb\":" + String(sdUsedMB) + ",";
        json += "\"sd_free_mb\":" + String(sdFreeMB) + ",";
        json += "\"total_photos\":" + String(photoPlaylist.size()) + ",";
        json += "\"current_index\":" + String(currentPhotoIndex) + ",";
        String curName = (currentPhotoIndex >= 0 && currentPhotoIndex < (int)photoPlaylist.size())
                         ? photoPlaylist[currentPhotoIndex] : "";
        json += "\"current_file\":\"" + curName + "\",";
        json += "\"paused\":" + String(isPaused ? "true" : "false") + ",";
        json += "\"interval\":" + String(slideIntervalSec) + ",";
        json += "\"order\":" + String(orderMode) + ",";
        json += "\"fit\":" + String(fitMode) + ",";
        json += "\"trans\":" + String(transitionStyle);
        json += "}";

        request->send(200, "application/json", json);
    });

    // 3. Photo List API: JSON array
    server.on("/api/photos", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!sdMounted) {
            request->send(503, "application/json", "[]");
            return;
        }

        String json = "[";
        if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1500)) == pdTRUE) {
            for (size_t i = 0; i < photoPlaylist.size(); i++) {
                String fullPath = String(PHOTO_DIR) + "/" + photoPlaylist[i];
                File f = SD.open(fullPath, FILE_READ);
                size_t sz = f ? f.size() : 0;
                if (f) f.close();

                String thumbPath = String(PHOTO_DIR) + "/" + photoPlaylist[i].substring(0, photoPlaylist[i].lastIndexOf('.')) + ".thumb.jpg";
                bool hasThumb = SD.exists(thumbPath);
                if (i > 0) json += ",";
                json += "{\"name\":\"" + photoPlaylist[i] + "\",\"size\":" + String(sz) + ",\"hasThumb\":" + String(hasThumb ? "true" : "false") + "}";
            }
            xSemaphoreGive(sdMutex);
        }
        json += "]";
        request->send(200, "application/json", json);
    });

    // 4. Photo View API (full original image)
    server.on("/api/view", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!sdMounted || !request->hasParam("file")) {
            request->send(400, "text/plain", "Bad Request");
            return;
        }

        String filename = request->getParam("file")->value();
        String safeName = sanitizeFilename(filename);
        if (safeName.length() == 0) {
            request->send(403, "text/plain", "Invalid path");
            return;
        }

        String fullPath = String(PHOTO_DIR) + "/" + safeName;
        size_t fsize = 0;
        if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1500)) == pdTRUE) {
            File f = SD.open(fullPath, FILE_READ);
            if (f && !f.isDirectory()) fsize = f.size();
            if (f) f.close();
            xSemaphoreGive(sdMutex);
        }
        if (fsize == 0) {
            request->send(404, "text/plain", "Not Found");
            return;
        }

        String contentType = "application/octet-stream";
        String lower = safeName;
        lower.toLowerCase();
        if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) contentType = "image/jpeg";
        else if (lower.endsWith(".png")) contentType = "image/png";
        else if (lower.endsWith(".bmp")) contentType = "image/bmp";

        // ETag lets the browser reuse its cache (300 s) instead of re-downloading on refresh.
        String etag = "\"" + String(fsize) + "-" + safeName + "\"";
        String inm = request->header("If-None-Match");
        if (inm.length() > 0 && inm.indexOf(etag) >= 0) {
            request->send(304, "text/plain", "");
            return;
        }

        // Pass the filesystem, not a live File. AsyncWebServer owns the response handle.
        AsyncWebServerResponse* resp = request->beginResponse(SD, fullPath, contentType);
        resp->addHeader("Cache-Control", "public, max-age=300");
        resp->addHeader("ETag", etag);
        request->send(resp);
    });

    server.on("/api/thumb", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!sdMounted || !request->hasParam("file")) {
            request->send(400, "text/plain", "Bad Request");
            return;
        }
        String source = sanitizeFilename(request->getParam("file")->value());
        if (source.length() == 0) {
            request->send(403, "text/plain", "Invalid path");
            return;
        }
        int dot = source.lastIndexOf('.');
        String thumbName = (dot > 0 ? source.substring(0, dot) : source) + ".thumb.jpg";
        String thumbPath = String(PHOTO_DIR) + "/" + thumbName;
        String originalPath = String(PHOTO_DIR) + "/" + source;
        bool thumbExists = false;
        if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1500)) == pdTRUE) {
            thumbExists = SD.exists(thumbPath);
            xSemaphoreGive(sdMutex);
        }
        String path = thumbExists ? thumbPath : originalPath;
        size_t imageSize = 0;
        if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1500)) == pdTRUE) {
            File imageFile = SD.open(path, FILE_READ);
            if (imageFile && !imageFile.isDirectory()) imageSize = imageFile.size();
            if (imageFile) imageFile.close();
            xSemaphoreGive(sdMutex);
        }
        if (imageSize == 0) {
            request->send(404, "text/plain", "Thumbnail not found");
            return;
        }
        String version = request->hasParam("v") ? request->getParam("v")->value() : String("0");
        String etag = "\"" + String(source) + (thumbExists ? "-thumb-" : "-original-") +
                      String(imageSize) + "-" + version + "\"";
        String inm = request->header("If-None-Match");
        if (inm == etag) {
            request->send(304, "text/plain", "");
            return;
        }
        AsyncWebServerResponse* resp = request->beginResponse(SD, path, "image/jpeg");
        resp->addHeader("Cache-Control", "public, max-age=300");
        resp->addHeader("ETag", etag);
        request->send(resp);
    });

    // 5. Upload Handler: Multipart file stream to SD
    // Uses file-scope state, NOT request->_tempObject: the request destructor calls free()
    // on _tempObject, so allocating it with new/delete caused heap corruption and unsigned
    // watchdog aborts (corrupted backtraces) after a client aborted mid-upload.
    server.on("/api/upload", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            // Upload state is reset in the final-chunk callback.
            request->send(uploadRequestFailed ? 500 : 200, "text/plain",
                          uploadRequestFailed ? "Upload incomplete" : "Upload complete");
        },
        [](AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
            lastUploadActivity = millis();

            if (!sdMounted) {
                uploadRequestFailed = true;
                isUploading = false;
                return;
            }

            if (!index) {
                isUploading = true;
                uploadRequestFailed = false;
                if (uploadFile) uploadFile.close();
                uploadFileOk = false;

                String safeName = sanitizeFilename(filename);
                if (safeName.length() == 0) {
                    Serial.println("[UPLOAD] Rejected filename");
                    uploadFileOk = false;
                    uploadRequestFailed = true;
                    isUploading = false;
                    return;
                }

                String fullPath = String(PHOTO_DIR) + "/" + safeName;
                if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
                    // FILE_WRITE appends on Arduino SD; remove any prior file first,
                    // otherwise re-uploading a name concatenates JPEG bytes and corrupts it.
                    if (SD.exists(fullPath)) SD.remove(fullPath);
                    uploadFile = SD.open(fullPath, FILE_WRITE);
                    uploadFileOk = (bool)uploadFile;
                    xSemaphoreGive(sdMutex);
                    Serial.printf("[UPLOAD] Starting upload: %s\n", fullPath.c_str());
                } else {
                    uploadFileOk = false;
                    uploadRequestFailed = true;
                }
            }

            if (uploadFileOk && uploadFile && len) {
                if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                    size_t written = uploadFile.write(data, len);
                    xSemaphoreGive(sdMutex);
                    if (written != len) {
                        Serial.printf("[UPLOAD] Short SD write: %u of %u bytes for %s\n",
                                      (unsigned)written, (unsigned)len, filename.c_str());
                        uploadFileOk = false;
                        uploadRequestFailed = true;
                    }
                } else {
                    Serial.printf("[UPLOAD] SD busy; failed to write chunk for %s\n", filename.c_str());
                    uploadFileOk = false;
                    uploadRequestFailed = true;
                }
            }

            if (final) {
                String safeName = sanitizeFilename(filename);
                String fullPath = String(PHOTO_DIR) + "/" + safeName;
                bool complete = uploadFileOk && (bool)uploadFile;
                if (uploadFile) {
                    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
                        uploadFile.close();
                        xSemaphoreGive(sdMutex);
                    } else {
                        complete = false;
                    }
                }
                uploadFileOk = false;
                isUploading = false;
                if (complete) {
                    Serial.printf("[UPLOAD] Finished writing photo: %s\n", fullPath.c_str());
                    scanPhotos();
                    forcePhotoName = safeName;
                } else {
                    Serial.printf("[UPLOAD] Incomplete photo removed: %s\n", fullPath.c_str());
                    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
                        SD.remove(fullPath);
                        xSemaphoreGive(sdMutex);
                    }
                }
            }
        }
    );

    // 6. Delete Individual Photo
    server.on("/api/delete", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!sdMounted || !request->hasParam("file")) {
            request->send(400, "text/plain", "Bad Request");
            return;
        }

        String filename = request->getParam("file")->value();
        String safeName = sanitizeFilename(filename);
        if (safeName.length() == 0) {
            request->send(403, "text/plain", "Forbidden");
            return;
        }

        String fullPath = String(PHOTO_DIR) + "/" + safeName;
        bool deleted = false;
        String thumbPath = String(PHOTO_DIR) + "/" + safeName.substring(0, safeName.lastIndexOf('.')) + ".thumb.jpg";

        if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
            if (SD.exists(fullPath)) {
                deleted = SD.remove(fullPath);
                if (SD.exists(thumbPath)) SD.remove(thumbPath);
            }
            xSemaphoreGive(sdMutex);
        }

        if (deleted) {
            Serial.printf("[DELETE] Removed: %s\n", fullPath.c_str());
            scanPhotos();
            if (photoPlaylist.size() > 0) {
                advanceSlide(true);
                showCurrentPhoto();
            } else {
                currentScreen = SCREEN_NO_PHOTOS;
                drawNoPhotosScreen();
            }
            request->send(200, "text/plain", "Deleted");
        } else {
            request->send(500, "text/plain", "Failed to delete");
        }
    });

    // 7. Delete All Photos
    server.on("/api/deleteall", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!sdMounted) {
            request->send(503, "text/plain", "SD not mounted");
            return;
        }

        int count = 0;
        if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(3000)) == pdTRUE) {
            for (size_t i = 0; i < photoPlaylist.size(); i++) {
                String fullPath = String(PHOTO_DIR) + "/" + photoPlaylist[i];
                String thumbPath = String(PHOTO_DIR) + "/" + photoPlaylist[i].substring(0, photoPlaylist[i].lastIndexOf('.')) + ".thumb.jpg";
                if (SD.remove(fullPath)) count++;
                if (SD.exists(thumbPath)) SD.remove(thumbPath);
            }
            xSemaphoreGive(sdMutex);
        }

        Serial.printf("[DELETE] Removed all %d photos\n", count);
        scanPhotos();
        currentScreen = SCREEN_NO_PHOTOS;
        drawNoPhotosScreen();
        request->send(200, "text/plain", "Deleted all");
    });

    // 8. Remote Control (Next, Prev, Play, Pause, Show)
    server.on("/api/control", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!request->hasParam("action")) {
            request->send(400, "text/plain", "Missing action");
            return;
        }

        String act = request->getParam("action")->value();
        if (act == "next") {
            forceNextSlide = true;
            isPaused = false;
        } else if (act == "prev") {
            advanceSlide(false);
            showCurrentPhoto();
            lastSlideChangeTime = millis();
        } else if (act == "play") {
            isPaused = false;
            saveSettingsToNVS();
        } else if (act == "pause") {
            isPaused = true;
            saveSettingsToNVS();
        } else if (act == "refresh") {
            showCurrentPhoto();
        } else if (act == "show" && request->hasParam("name")) {
            forcePhotoName = sanitizeFilename(request->getParam("name")->value());
        }

        request->send(200, "text/plain", "OK");
    });

    // 9. Settings Configuration API
    server.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (request->hasParam("interval", true)) {
            uint32_t val = request->getParam("interval", true)->value().toInt();
            if (val >= 1 && val <= 3600) slideIntervalSec = val;
        }
        if (request->hasParam("order", true)) {
            orderMode = request->getParam("order", true)->value().toInt();
        }
        if (request->hasParam("fit", true)) {
            fitMode = request->getParam("fit", true)->value().toInt();
        }
        if (request->hasParam("trans", true)) {
            transitionStyle = request->getParam("trans", true)->value().toInt();
        }

        saveSettingsToNVS();
        request->send(200, "text/plain", "Settings saved");
    });

    // 10. WiFi Setup API
    server.on("/api/wifi", HTTP_POST, [](AsyncWebServerRequest* request) {
        String ssid = "";
        String pass = "";

        if (request->hasParam("ssid", true)) {
            ssid = request->getParam("ssid", true)->value();
        }
        if (request->hasParam("pass", true)) {
            pass = request->getParam("pass", true)->value();
        }

        if (ssid.length() > 0 && ssid.length() <= 32) {
            prefs.begin(PREF_WIFI_NS, false);
            prefs.putString(PREF_KEY_SSID, ssid);
            prefs.putString(PREF_KEY_PASS, pass);
            prefs.end();

            Serial.printf("[WIFI] New credentials saved for SSID: '%s'. Rebooting...\n", ssid.c_str());
            request->send(200, "text/plain", "WiFi saved. Rebooting...");
            delay(1500);
            ESP.restart();
        } else {
            request->send(400, "text/plain", "Invalid SSID");
        }
    });

    server.begin();
    Serial.println("[HTTP] Web server started on port 80");
}

// ============================================================
// Utilities: Filename Sanitization & Path Traversal Guard
// ============================================================
String sanitizeFilename(const String& filename) {
    String clean = "";
    int lastSlash = filename.lastIndexOf('/');
    int lastBackslash = filename.lastIndexOf('\\');
    int start = max(lastSlash, lastBackslash);
    String base = (start >= 0) ? filename.substring(start + 1) : filename;

    // Limit filename length (FAT32 max is 255, but we'll be conservative)
    if (base.length() > 128) {
        base = base.substring(0, 128);
    }

    for (size_t i = 0; i < base.length(); i++) {
        char c = base[i];
        // Allow alphanumeric, dot, underscore, hyphen - replace spaces with underscore
        if (isalnum(c) || c == '.' || c == '_' || c == '-') {
            clean += c;
        } else if (c == ' ') {
            clean += '_';
        }
        // All other characters are dropped
    }
    clean.trim();

    // Disallow relative dot sequences (path traversal)
    if (clean.indexOf("..") >= 0 || clean.length() == 0) {
        return "";
    }

    // Disallow hidden files and system names
    if (clean.startsWith(".") || clean.equalsIgnoreCase("CON") ||
        clean.equalsIgnoreCase("PRN") || clean.equalsIgnoreCase("AUX") ||
        clean.equalsIgnoreCase("NUL") || clean.equalsIgnoreCase("COM1") ||
        clean.equalsIgnoreCase("COM2") || clean.equalsIgnoreCase("COM3") ||
        clean.equalsIgnoreCase("COM4") || clean.equalsIgnoreCase("COM5") ||
        clean.equalsIgnoreCase("COM6") || clean.equalsIgnoreCase("COM7") ||
        clean.equalsIgnoreCase("COM8") || clean.equalsIgnoreCase("COM9") ||
        clean.equalsIgnoreCase("LPT1") || clean.equalsIgnoreCase("LPT2") ||
        clean.equalsIgnoreCase("LPT3") || clean.equalsIgnoreCase("LPT4") ||
        clean.equalsIgnoreCase("LPT5") || clean.equalsIgnoreCase("LPT6") ||
        clean.equalsIgnoreCase("LPT7") || clean.equalsIgnoreCase("LPT8") ||
        clean.equalsIgnoreCase("LPT9")) {
        return "";
    }

    // Ensure it has a valid image extension. Thumbnail derivatives use .thumb.jpg.
    String lower = clean;
    lower.toLowerCase();
    if (!(lower.endsWith(".jpg") || lower.endsWith(".jpeg") ||
          lower.endsWith(".png") || lower.endsWith(".bmp") ||
          lower.endsWith(".thumb.jpg"))) {
        return "";
    }

    return clean;
}

