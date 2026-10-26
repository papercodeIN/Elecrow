/*
 * Elecrow 2.8" ESP32 Solo Miner LCD Display - Digital Photo Frame
 *
 * Board: Elecrow 2.8" ESP32 Solo Miner LCD Display (CrowPanel 2.8")
 * Display: 320x240 ILI9341V TFT (Landscape, Rotation 1) via HSPI (SPI2)
 * Storage: Built-in MicroSD card slot via VSPI (SPI3)
 *
 * Features:
 * - Full-screen photo slideshow directly from SD card (JPEG, PNG, BMP)
 * - Auto-scaling: Letterbox (aspect-ratio fit) or Fill & Crop
 * - Configurable transition styles (Wipe, Blinds, Curtain, Fade, Instant)
 * - Configurable slide interval and play order (Sequential vs Shuffle)
 * - Web management interface with drag-and-drop multi-file upload
 * - Streaming upload directly to SD card without RAM buffering
 * - Live photo frame remote control (Next, Prev, Pause/Resume, Jump to photo)
 * - Thumbnail gallery with preview and delete functions
 * - Dual WiFi mode: Station mode (with auto-reconnect) & SoftAP captive portal
 * - Non-blocking design with thread-safe SD mutex protection
 * - Persistent configuration in NVS Preferences
 *
 * Required Libraries (Install via Arduino Library Manager):
 * - TFT_eSPI by Bodmer (v2.5.43+)
 * - ESPAsyncWebServer by me-no-dev (v1.2.3+)
 * - AsyncTCP by me-no-dev (v1.1.1+)
 * - TJpg_Decoder by Bodmer (v1.1.0+)
 * - pngle by kikuchan98 (v1.1.0+)
 *
 * IMPORTANT TFT_eSPI SETUP:
 * Open your Arduino libraries folder -> TFT_eSPI/User_Setup.h and set:
 *   #define ILI9341_DRIVER
 *   #define TFT_WIDTH  240
 *   #define TFT_HEIGHT 320
 *   #define TFT_MISO   12
 *   #define TFT_MOSI   13
 *   #define TFT_SCLK   14
 *   #define TFT_CS     15
 *   #define TFT_DC     2
 *   #define TFT_RST    -1
 *   #define TFT_BL     27
 *   #define TOUCH_CS   33
 *   #define USE_HSPI_PORT
 *   #define SPI_FREQUENCY 20000000
 *   #define LOAD_GLCD
 *   #define LOAD_FONT2
 */

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <ArduinoOTA.h>
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
uint8_t fitMode = DEFAULT_FIT_MODE;             // 0: Letterbox, 1: Crop/Fill
uint8_t transitionStyle = DEFAULT_TRANSITION;   // 0: Instant, 1: Wipe, etc.
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
int sdRetryCount = 0;
int wifiFailCount = 0;

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

// ============================================================
// Setup
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n==============================================");
    Serial.println("  Elecrow 2.8\" ESP32 Digital Photo Frame");
    Serial.println("==============================================");

    sdMutex = xSemaphoreCreateMutex();
    randomSeed(analogRead(34) ^ micros());

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH); // Backlight ON

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

    // 5. Non-blocking WiFi Reconnect Management
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
        } else if (!isPaused && !isUploading) {
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

        if (shouldChange) {
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
    tft.fillScreen(0x0841); // Deep slate blue

    // Header gradient box
    tft.fillRect(0, 0, SCREEN_WIDTH, 48, 0x18E3);
    tft.setTextColor(0x3DFE, 0x18E3); // Bright sky cyan
    tft.setTextSize(2);
    tft.drawCentreString("ELECROW PHOTO FRAME", SCREEN_WIDTH / 2, 8, 2);

    // Subtitle
    tft.setTextColor(TFT_WHITE, 0x0841);
    tft.drawCentreString("2.8\" ESP32 IPS Display", SCREEN_WIDTH / 2, 70, 2);

    // Status box
    tft.fillRoundRect(20, 110, SCREEN_WIDTH - 40, 75, 8, 0x10A2);
    tft.drawRoundRect(20, 110, SCREEN_WIDTH - 40, 75, 8, 0x2965);

    tft.setTextColor(0x9CF3, 0x10A2);
    tft.setTextSize(1);
    tft.drawCentreString("System Initializing...", SCREEN_WIDTH / 2, 126, 2);
    tft.drawCentreString("Mounting SD Card & WiFi...", SCREEN_WIDTH / 2, 150, 2);

    // Footer
    tft.setTextColor(0x52AA, 0x0841);
    tft.drawCentreString("v1.0.0 * Elecrow Studio", SCREEN_WIDTH / 2, 215, 1);
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

    tft.fillRect(0, 0, SCREEN_WIDTH, 44, 0x01E8);
    tft.setTextColor(TFT_WHITE, 0x01E8);
    tft.drawCentreString("WIFI SETUP REQUIRED", SCREEN_WIDTH / 2, 10, 2);

    tft.fillRoundRect(16, 56, SCREEN_WIDTH - 32, 134, 8, 0x10A2);
    tft.drawRoundRect(16, 56, SCREEN_WIDTH - 32, 134, 8, 0x07FF);

    tft.setTextColor(0x07FF, 0x10A2);
    tft.drawCentreString("1. Connect to WiFi Access Point:", SCREEN_WIDTH / 2, 68, 1);
    tft.setTextColor(TFT_YELLOW, 0x10A2);
    tft.drawCentreString(AP_SSID, SCREEN_WIDTH / 2, 86, 2);

    tft.setTextColor(0x07FF, 0x10A2);
    tft.drawCentreString("2. Open Browser & Configure Home WiFi:", SCREEN_WIDTH / 2, 118, 1);
    tft.setTextColor(TFT_WHITE, 0x10A2);
    tft.drawCentreString("http://192.168.4.1", SCREEN_WIDTH / 2, 138, 2);

    tft.setTextColor(0x9CF3, 0x10A2);
    tft.drawCentreString("Or tap captive portal notification", SCREEN_WIDTH / 2, 166, 1);

    tft.setTextColor(0x52AA, 0x0841);
    tft.drawCentreString("Waiting for user setup...", SCREEN_WIDTH / 2, 214, 1);
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
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

    if (SD.begin(SD_CS, SPI, SD_SPI_SPEED)) {
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

    static bool introShown = false;
    if (!introShown) {
        introShown = true;
        tft.fillScreen(0x0841);
        tft.fillRect(0, 0, SCREEN_WIDTH, 48, 0x18E3);
        tft.setTextColor(TFT_WHITE, 0x18E3);
        tft.drawCentreString("PHOTO FRAME READY", SCREEN_WIDTH / 2, 12, 2);
        tft.setTextColor(0x9CF3, 0x0841);
        tft.drawCentreString("Open this address in a browser", SCREEN_WIDTH / 2, 78, 2);
        String url = "http://" + (apMode ? WiFi.softAPIP().toString() : currentIP);
        if (url == "http://") url = "Connect to WiFi...";
        tft.setTextColor(TFT_YELLOW, 0x0841);
        tft.drawCentreString(url.c_str(), SCREEN_WIDTH / 2, 112, 4);
        tft.setTextColor(TFT_WHITE, 0x0841);
        tft.drawCentreString("Phone/computer must use the same WiFi", SCREEN_WIDTH / 2, 164, 1);
        tft.setTextColor(0x9CF3, 0x0841);
        tft.drawCentreString("Starting slideshow...", SCREEN_WIDTH / 2, 210, 1);
        delay(5000);
    }

    String filename = photoPlaylist[currentPhotoIndex];
    String fullPath = String(PHOTO_DIR) + "/" + filename;

    Serial.printf("[SLIDESHOW] Displaying [%d/%d]: %s\n",
                  currentPhotoIndex + 1, photoPlaylist.size(), fullPath.c_str());

    bool ok = renderImageFile(fullPath.c_str(), (FitMode)fitMode, (TransitionStyle)transitionStyle);

    if (!ok) {
        consecutiveFailures++;
        Serial.printf("[SLIDESHOW] Corrupted or unsupported image: %s (fail count: %d)\n",
                      fullPath.c_str(), consecutiveFailures);

        if (consecutiveFailures >= (int)photoPlaylist.size()) {
            Serial.printf("[SLIDESHOW] All %d photo(s) in playlist failed to render. Displaying error notice.\n",
                          (int)photoPlaylist.size());
            drawUnreadablePhotoScreen(filename.c_str());
        } else {
            advanceSlide(true);
        }
    } else {
        consecutiveFailures = 0;
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
        String contentType = "application/octet-stream";
        String lower = safeName;
        lower.toLowerCase();
        if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) contentType = "image/jpeg";
        else if (lower.endsWith(".png")) contentType = "image/png";
        else if (lower.endsWith(".bmp")) contentType = "image/bmp";

        if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1500)) == pdTRUE) {
            File f = SD.open(fullPath, FILE_READ);
            xSemaphoreGive(sdMutex);
            if (!f || f.isDirectory()) {
                request->send(404, "text/plain", "Not Found");
                return;
            }
            request->send(f, fullPath, contentType);
        } else {
            request->send(503, "text/plain", "SD busy");
        }
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
        if (request->header("If-None-Match") == etag) {
            request->send(304, "text/plain", "");
            return;
        }
        AsyncWebServerResponse* resp = request->beginResponse(SD, path, "image/jpeg");
        resp->addHeader("Cache-Control", "public, max-age=300");
        resp->addHeader("ETag", etag);
        request->send(resp);
    });

    // 5. Upload Handler: Multipart file stream to SD
    server.on("/api/upload", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            isUploading = false;
            request->send(requestFailed ? 500 : 200, "text/plain",
                          requestFailed ? "Upload incomplete" : "Upload success");
        },
        [](AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
            static File uploadFile;
            static bool fileOk = false;
            static bool requestFailed = false;

            if (!sdMounted) {
                requestFailed = true;
                isUploading = false;
                return;
            }

            if (!index) {
                isUploading = true;
                requestFailed = false;
                String safeName = sanitizeFilename(filename);
                if (safeName.length() == 0) {
                    fileOk = false;
                    requestFailed = true;
                    Serial.println("[UPLOAD] Rejected filename");
                    return;
                }

                String fullPath = String(PHOTO_DIR) + "/" + safeName;
                if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
                    // FILE_WRITE appends on Arduino SD; replace old uploads instead of
                    // appending new JPEG bytes to an existing photo with the same name.
                    if (SD.exists(fullPath)) SD.remove(fullPath);
                    uploadFile = SD.open(fullPath, FILE_WRITE);
                    fileOk = (bool)uploadFile;
                    xSemaphoreGive(sdMutex);
                    Serial.printf("[UPLOAD] Starting upload: %s\n", fullPath.c_str());
                } else {
                    fileOk = false;
                    requestFailed = true;
                }
            }

            if (fileOk && uploadFile && len) {
                if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                    size_t written = uploadFile.write(data, len);
                    xSemaphoreGive(sdMutex);
                    if (written != len) {
                        Serial.printf("[UPLOAD] Short SD write: %u of %u bytes for %s\n",
                                      (unsigned)written, (unsigned)len, filename.c_str());
                        fileOk = false;
                        requestFailed = true;
                    }
                } else {
                    Serial.printf("[UPLOAD] SD busy; failed to write chunk for %s\n", filename.c_str());
                    fileOk = false;
                    requestFailed = true;
                }
            }

            if (final) {
                String safeName = sanitizeFilename(filename);
                String fullPath = String(PHOTO_DIR) + "/" + safeName;
                bool complete = fileOk && (bool)uploadFile;
                if (uploadFile) {
                    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
                        uploadFile.close();
                        xSemaphoreGive(sdMutex);
                    } else {
                        complete = false;
                    }
                }
                fileOk = false;
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

    for (size_t i = 0; i < base.length(); i++) {
        char c = base[i];
        if (isalnum(c) || c == '.' || c == '_' || c == '-' || c == ' ') {
            clean += c;
        }
    }
    clean.trim();

    // Disallow relative dot sequences
    if (clean.indexOf("..") >= 0 || clean.length() == 0) {
        return "";
    }
    String lower = clean;
    lower.toLowerCase();
    if (!(lower.endsWith(".jpg") || lower.endsWith(".jpeg") ||
          lower.endsWith(".png") || lower.endsWith(".bmp") ||
          lower.endsWith(".thumb.jpg"))) {
        return "";
    }
    return clean;
}

