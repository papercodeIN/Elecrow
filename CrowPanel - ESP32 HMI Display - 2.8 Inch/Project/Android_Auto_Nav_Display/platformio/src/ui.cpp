// ===========================================================================
// ui.cpp - nav card rendering, themes, gestures, TURN NOW flash
//
// Rendering strategy: TFT_eSprite only for the arrow panel (120x190, ~45 KB
// of heap - the board has no PSRAM); everything else is partial redraws of
// small rects straight to the TFT. TFT_eSprite derives from TFT_eSPI, so the
// glyph helpers could render into either surface - we keep the sprite as the
// single source (it is what makes the flash flicker-free).
// ===========================================================================

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <math.h>
#include "ui.h"
#include "config.h"

// ----------------------------- Theme --------------------------------------
namespace {

// TFT_eSPI colours are RGB565; define themes via 8-bit triplets for clarity.
#define RGB565(r, g, b) ((uint16_t)(((((uint32_t)(r)) >> 3) << 11) | \
                                    ((((uint32_t)(g)) >> 2) << 5)  | \
                                    (((uint32_t)(b)) >> 3)))

struct Theme {
    uint16_t bg;
    uint16_t statusBg;
    uint16_t panelNav;
    uint16_t panelNear;
    uint16_t panelNow;
    uint16_t panelNowAlt;   // alternate flash colour
    uint16_t textMain;
    uint16_t textMuted;
    uint16_t glyph;
    uint16_t progressFill;
    uint16_t progressTrack;
    uint16_t bannerBg;
    uint16_t bannerText;
};

constexpr Theme kDay = {
    RGB565(8, 20, 40),        // bg (dark navy)
    RGB565(4, 10, 20),        // statusBg
    RGB565(66, 133, 244),     // panelNav (Maps blue)
    RGB565(178, 106, 0),      // panelNear (amber)
    RGB565(197, 34, 31),      // panelNow (red)
    RGB565(127, 22, 15),      // panelNowAlt (dark red flash)
    0xFFFF,                   // textMain
    RGB565(154, 160, 166),    // textMuted
    0xFFFF,                   // glyph
    RGB565(138, 180, 248),    // progressFill
    RGB565(10, 42, 64),       // progressTrack
    RGB565(58, 13, 13),       // bannerBg
    RGB565(255, 180, 171)     // bannerText
};

constexpr Theme kNight = {
    0x0000,                   // bg
    RGB565(10, 10, 10),       // statusBg
    RGB565(16, 58, 92),       // panelNav
    RGB565(110, 74, 0),       // panelNear
    RGB565(127, 29, 29),      // panelNow
    RGB565(74, 16, 16),       // panelNowAlt
    RGB565(232, 234, 237),    // textMain
    RGB565(128, 134, 139),    // textMuted
    RGB565(241, 243, 244),    // glyph
    RGB565(91, 126, 168),     // progressFill
    RGB565(16, 20, 24),       // progressTrack
    RGB565(42, 10, 10),       // bannerBg
    RGB565(229, 185, 179)     // bannerText
};

// ----------------------------- State --------------------------------------
TFT_eSPI    tft;
TFT_eSprite arrowSpr = TFT_eSprite(&tft);
bool        s_spriteOk = false;

Theme   s_th;
bool    s_night = false;
uint8_t s_brightIdx = 0;

bool     s_link = false;
bool     s_haveNav = false;
NavData  s_nav;
bool     s_sigLost = false;
uint32_t s_lastNavMs = 0;

Urgency  s_urg = URG_NONE;
bool     s_flashOn = false;
uint32_t s_lastFlashMs = 0;
int32_t  s_d0 = 0;             // distance snapshot when the maneuver changed
uint8_t  s_pct = 0;

// Last drawn frame (for diff redraws)
struct Last {
    bool      link;
    bool      haveNav;
    NavPhase  phase;
    Maneuver  m;
    Urgency   urg;
    bool      flash;
    bool      sigLost;
    uint8_t   pct;
    uint8_t   speed;
    char      ds[20];
    char      street[56];
    char      then[48];
    char      eta[12];
    char      route[16];
};
Last s_last;

void resetLast() {
    memset(&s_last, 0, sizeof(s_last));
    s_last.m = MNV_UNKNOWN;
    s_last.urg = URG_NONE;
}

// ----------------------------- Small helpers ------------------------------
void clearRect(int32_t x, int32_t y, int32_t w, int32_t h) {
    tft.fillRect(x, y, w, h, s_th.bg);
}

// Thick line by drawing parallel 1 px lines along the perpendicular.
void wideLine(TFT_eSPI& g, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
              int32_t w, uint32_t color) {
    float dx = (float)(x1 - x0);
    float dy = (float)(y1 - y0);
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.5f) {
        g.fillRect(x0 - w / 2, y0 - w / 2, w, w, color);
        return;
    }
    float nx = -dy / len;
    float ny = dx / len;
    int32_t half = w / 2;
    for (int32_t i = -half; i <= half; i++) {
        g.drawLine((int32_t)lrintf(x0 + nx * i), (int32_t)lrintf(y0 + ny * i),
                   (int32_t)lrintf(x1 + nx * i), (int32_t)lrintf(y1 + ny * i),
                   color);
    }
}

// Arrow head centred at (cx, cy), pointing at angleDeg (0 = up, clockwise).
void arrowHead(TFT_eSPI& g, float cx, float cy, float angleDeg,
               float length, float halfW, uint32_t color) {
    float a = angleDeg * (float)M_PI / 180.0f;
    float dx = sinf(a);
    float dy = -cosf(a);
    float tipX = cx + dx * length;
    float tipY = cy + dy * length;
    float px = -dy;   // perpendicular unit vector
    float py = dx;
    g.fillTriangle((int16_t)lrintf(tipX), (int16_t)lrintf(tipY),
                   (int16_t)lrintf(cx + px * halfW), (int16_t)lrintf(cy + py * halfW),
                   (int16_t)lrintf(cx - px * halfW), (int16_t)lrintf(cy - py * halfW),
                   color);
}

// ----------------------------- Maneuver glyphs ----------------------------
// Glyph box inside the 120x190 sprite: cx = 60, y from ~30 to ~160.
// Direction angles: 0 = up, positive = clockwise (i.e. towards the right).
void drawManeuverGlyph(TFT_eSPI& g, Maneuver m, uint32_t color, uint32_t panelBg) {
    const int32_t w = 9;   // shaft width

    switch (m) {
        case MNV_ST:
            wideLine(g, 60, 160, 60, 78, w, color);
            arrowHead(g, 60, 78, 0, 32, 18, color);
            break;

        case MNV_TL:
            wideLine(g, 60, 160, 60, 110, w, color);
            wideLine(g, 60, 110, 34, 110, w, color);
            arrowHead(g, 34, 110, -90, 26, 16, color);
            break;

        case MNV_TR:
            wideLine(g, 60, 160, 60, 110, w, color);
            wideLine(g, 60, 110, 86, 110, w, color);
            arrowHead(g, 86, 110, 90, 26, 16, color);
            break;

        case MNV_SL:
        case MNV_MEL:
            wideLine(g, 60, 160, 60, 118, w, color);
            wideLine(g, 60, 118, 84, 94, w, color);
            arrowHead(g, 88, 90, 45, 26, 16, color);
            break;

        case MNV_SR:
        case MNV_MER:
            wideLine(g, 60, 160, 60, 118, w, color);
            wideLine(g, 60, 118, 36, 94, w, color);
            arrowHead(g, 32, 90, -45, 26, 16, color);
            break;

        case MNV_SHL:
        case MNV_EXL:
            wideLine(g, 60, 160, 60, 112, w, color);
            wideLine(g, 60, 112, 40, 92, w, color);
            arrowHead(g, 38, 90, -135, 26, 16, color);
            break;

        case MNV_SHR:
        case MNV_EXR:
            wideLine(g, 60, 160, 60, 112, w, color);
            wideLine(g, 60, 112, 80, 92, w, color);
            arrowHead(g, 82, 90, 135, 26, 16, color);
            break;

        case MNV_FKL:
            wideLine(g, 52, 160, 40, 96, w, color);
            arrowHead(g, 38, 92, -40, 26, 16, color);
            wideLine(g, 70, 160, 76, 120, w - 2, color);
            arrowHead(g, 77, 116, 20, 20, 13, color);
            break;

        case MNV_FKR:
            wideLine(g, 68, 160, 80, 96, w, color);
            arrowHead(g, 82, 92, 40, 26, 16, color);
            wideLine(g, 50, 160, 44, 120, w - 2, color);
            arrowHead(g, 43, 116, -20, 20, 13, color);
            break;

        case MNV_UT:
            wideLine(g, 46, 160, 46, 64, w, color);
            wideLine(g, 46, 64, 74, 64, w, color);
            wideLine(g, 74, 64, 74, 118, w, color);
            arrowHead(g, 74, 118, 180, 26, 16, color);
            break;

        case MNV_RAB:
            for (int16_t r = 25; r <= 27; r++) {
                g.drawCircle(60, 95, r, color);
            }
            wideLine(g, 60, 160, 60, 124, w, color);
            wideLine(g, 76, 79, 94, 61, w, color);
            arrowHead(g, 97, 58, 45, 24, 15, color);
            break;

        case MNV_ARR:
            g.fillCircle(60, 80, 20, color);
            g.fillCircle(60, 80, 8, panelBg);
            g.fillTriangle(46, 92, 74, 92, 60, 118, color);
            break;

        default:
            g.setTextColor(color, panelBg);
            g.setFreeFont(&FreeSansBold24pt7b);
            g.setTextDatum(MC_DATUM);
            g.drawCentreString("?", 60, 95, 1);
            g.setTextDatum(TL_DATUM);
            break;
    }
}

uint16_t panelColor(bool flashOn) {
    if (s_urg == URG_NOW) {
        return flashOn ? s_th.panelNowAlt : s_th.panelNow;
    }
    if (s_urg == URG_NEAR) {
        return s_th.panelNear;
    }
    return s_th.panelNav;
}

void drawArrowPanel() {
    uint16_t bg = panelColor(s_flashOn);
    if (s_spriteOk) {
        arrowSpr.fillSprite(bg);
        drawManeuverGlyph(arrowSpr, s_haveNav ? s_nav.m : MNV_UNKNOWN,
                          s_th.glyph, bg);
        arrowSpr.pushSprite(ARROW_PANEL_X, ARROW_PANEL_Y);
    } else {
        tft.fillRect(ARROW_PANEL_X, ARROW_PANEL_Y, ARROW_PANEL_W, ARROW_PANEL_H, bg);
    }
}

// ----------------------------- Status bar ---------------------------------
const char* phaseLabel(NavPhase p) {
    switch (p) {
        case PH_REROUTE: return "Recalculating...";
        case PH_ARRIVED: return "Arrived";
        default:         return "";
    }
}

void drawStatusBar() {
    tft.fillRect(0, 0, UI_WIDTH, STATUS_BAR_H, s_th.statusBg);
    tft.fillCircle(10, 11, 4, s_link ? RGB565(52, 168, 83) : RGB565(95, 99, 104));
    tft.setFreeFont(&FreeSans9pt7b);
    tft.setTextColor(s_th.textMuted, s_th.statusBg);
    tft.setTextDatum(ML_DATUM);
    tft.drawString("AA Nav", 20, 11, 1);
    if (s_haveNav) {
        tft.drawString(phaseLabel(s_nav.phase), 160, 11, 1);
        if (s_nav.speedKmh > 0) {
            char spd[12];
            snprintf(spd, sizeof(spd), "%u km/h", (unsigned)s_nav.speedKmh);
            tft.setTextDatum(MR_DATUM);
            tft.drawString(spd, 314, 11, 1);
        }
    }
    tft.setTextDatum(TL_DATUM);
}

// ----------------------------- Text sections ------------------------------
void drawDistance() {
    clearRect(122, 24, 198, 56);
    char ds[sizeof(s_nav.ds)];
    if (s_nav.ds[0]) {
        strlcpy(ds, s_nav.ds, sizeof(ds));
    } else if (s_haveNav && s_nav.d >= 0) {
        snprintf(ds, sizeof(ds), "%ld m", (long)s_nav.d);
    } else {
        ds[0] = '\0';
    }
    if (!ds[0]) {
        return;
    }
    tft.setTextColor(s_th.textMain, s_th.bg);
    tft.setFreeFont(&FreeSansBold24pt7b);
    tft.setTextDatum(MC_DATUM);
    tft.drawCentreString(ds, 221, 51, 1);
    tft.setTextDatum(TL_DATUM);
}

// Word-wrap `street` into two lines inside 194 px. Line 1 uses 12pt, the
// remainder is drawn in 9pt. Never destroys the source string while probing.
void drawStreet() {
    clearRect(122, 82, 198, 56);
    if (!s_nav.street[0]) {
        return;
    }
    const int32_t maxW = 194;
    char work[56];
    strlcpy(work, s_nav.street, sizeof(work));

    tft.setFreeFont(&FreeSans12pt7b);
    if (tft.textWidth(work) <= maxW) {
        tft.setTextColor(s_th.textMain, s_th.bg);
        tft.drawString(work, 124, 84, 1);
        return;
    }

    // Find the longest space-separated prefix that fits on line 1.
    char line1[56];
    char line2[56];
    line1[0] = line2[0] = '\0';
    size_t searchLen = strlen(work);
    char tmp[56];
    while (searchLen > 0) {
        // Last space within work[0..searchLen)
        char* sp = nullptr;
        for (size_t i = searchLen; i-- > 0;) {
            if (work[i] == ' ') {
                sp = &work[i];
                break;
            }
        }
        if (!sp) {
            break;
        }
        size_t keep = (size_t)(sp - work);
        if (keep == 0) {
            break;
        }
        memcpy(tmp, work, keep);
        tmp[keep] = '\0';
        if (tft.textWidth(tmp) <= maxW) {
            strlcpy(line1, tmp, sizeof(line1));
            strlcpy(line2, sp + 1, sizeof(line2));
            break;
        }
        searchLen = keep;
    }

    tft.setTextColor(s_th.textMain, s_th.bg);
    if (line1[0]) {
        tft.drawString(line1, 124, 84, 1);
    }
    tft.setFreeFont(&FreeSans9pt7b);
    while (line2[0] && tft.textWidth(line2) > maxW && strlen(line2) > 4) {
        strcpy(line2 + strlen(line2) - 4, "...");
    }
    if (line2[0]) {
        tft.drawString(line2, 124, 112, 1);
    }
}

void drawThen() {
    clearRect(122, 140, 198, 22);
    if (!s_nav.then[0]) {
        return;
    }
    char buf[56];
    snprintf(buf, sizeof(buf), "Then: %s", s_nav.then);
    tft.setFreeFont(&FreeSans9pt7b);
    while (tft.textWidth(buf) > 194 && strlen(buf) > 4) {
        strcpy(buf + strlen(buf) - 4, "...");
    }
    tft.setTextColor(s_th.textMuted, s_th.bg);
    tft.drawString(buf, 124, 144, 1);
}

// Right-hand column only: the left 120 px belong to the arrow panel.
void drawInfoRow() {
    clearRect(122, 188, 198, 22);
    tft.setFreeFont(&FreeSans9pt7b);
    tft.setTextColor(s_th.textMuted, s_th.bg);
    tft.setTextDatum(ML_DATUM);
    if (s_nav.eta[0]) {
        char buf[24];
        snprintf(buf, sizeof(buf), "ETA %s", s_nav.eta);
        tft.drawString(buf, 124, 199, 1);
    }
    if (s_nav.route[0]) {
        tft.setTextDatum(MR_DATUM);
        tft.drawString(s_nav.route, 316, 199, 1);
    }
    tft.setTextDatum(TL_DATUM);
}

void drawProgress() {
    tft.fillRect(0, PROGRESS_Y, UI_WIDTH, PROGRESS_H, s_th.progressTrack);
    if (s_haveNav && s_nav.phase == PH_NAV && s_pct > 0) {
        int32_t w = 1 + (int32_t)s_pct * (UI_WIDTH - 2) / 100;
        tft.fillRect(1, PROGRESS_Y, w, PROGRESS_H, s_th.progressFill);
    }
}

void drawBanner() {
    tft.fillRect(0, BANNER_Y, UI_WIDTH, BANNER_H, s_th.bannerBg);
    tft.setFreeFont(&FreeSans9pt7b);
    tft.setTextColor(s_th.bannerText, s_th.bannerBg);
    tft.setTextDatum(MC_DATUM);
    tft.drawCentreString("SIGNAL LOST - check the phone", 160, BANNER_Y + BANNER_H / 2, 1);
    tft.setTextDatum(TL_DATUM);
}

void clearBanner() {
    clearRect(0, BANNER_Y, UI_WIDTH, BANNER_H);
}

// ----------------------------- Screens ------------------------------------
void drawIdleScreen(const char* title, const char* line1, const char* line2) {
    tft.fillScreen(s_th.bg);
    drawStatusBar();
    tft.setTextColor(s_th.textMain, s_th.bg);
    tft.setFreeFont(&FreeSansBold24pt7b);
    tft.setTextDatum(MC_DATUM);
    tft.drawCentreString(title, 160, 92, 1);
    tft.setFreeFont(&FreeSans9pt7b);
    tft.setTextColor(s_th.textMuted, s_th.bg);
    tft.drawCentreString(line1, 160, 150, 1);
    tft.drawCentreString(line2, 160, 172, 1);
    tft.setTextDatum(TL_DATUM);
}

// ----------------------------- Nav card update ----------------------------
void updateNavCard(bool forceFull) {
    NavPhase phase  = s_haveNav ? s_nav.phase : PH_IDLE;
    Maneuver  m     = s_haveNav ? s_nav.m : MNV_UNKNOWN;
    uint8_t   speed = s_haveNav ? s_nav.speedKmh : 0;
    const char* ds     = s_haveNav ? s_nav.ds : "";
    const char* street = s_haveNav ? s_nav.street : "";
    const char* then   = s_haveNav ? s_nav.then : "";
    const char* eta    = s_haveNav ? s_nav.eta : "";
    const char* route  = s_haveNav ? s_nav.route : "";

    if (forceFull || s_last.link != s_link || s_last.haveNav != s_haveNav ||
        s_last.phase != phase || s_last.speed != speed) {
        drawStatusBar();
        s_last.link = s_link;
        s_last.haveNav = s_haveNav;
        s_last.phase = phase;
        s_last.speed = speed;
    }

    if (forceFull || s_last.m != m || s_last.urg != s_urg || s_last.flash != s_flashOn) {
        drawArrowPanel();
        s_last.m = m;
        s_last.urg = s_urg;
        s_last.flash = s_flashOn;
    }

    if (forceFull || strncmp(s_last.ds, ds, sizeof(s_last.ds)) != 0) {
        drawDistance();
        strlcpy(s_last.ds, ds, sizeof(s_last.ds));
    }

    if (forceFull || strncmp(s_last.street, street, sizeof(s_last.street)) != 0) {
        drawStreet();
        strlcpy(s_last.street, street, sizeof(s_last.street));
    }

    if (forceFull || strncmp(s_last.then, then, sizeof(s_last.then)) != 0) {
        drawThen();
        strlcpy(s_last.then, then, sizeof(s_last.then));
    }

    if (forceFull || strncmp(s_last.eta, eta, sizeof(s_last.eta)) != 0 ||
        strncmp(s_last.route, route, sizeof(s_last.route)) != 0) {
        drawInfoRow();
        strlcpy(s_last.eta, eta, sizeof(s_last.eta));
        strlcpy(s_last.route, route, sizeof(s_last.route));
    }

    uint8_t pct = 0;
    if (s_haveNav && s_nav.phase == PH_NAV && s_d0 > 0 && s_nav.d >= 0) {
        int32_t p = (int32_t)(100 - (int64_t)s_nav.d * 100 / s_d0);
        pct = (uint8_t)(p < 0 ? 0 : (p > 100 ? 100 : p));
    }
    if (forceFull || s_last.pct != pct) {
        s_pct = pct;
        s_last.pct = pct;
        drawProgress();
    }

    if (forceFull || s_last.sigLost != s_sigLost) {
        if (s_sigLost) {
            drawBanner();
        } else {
            clearBanner();
        }
        s_last.sigLost = s_sigLost;
    }
}

// ----------------------------- Brightness / gestures ----------------------
void applyBrightness() {
    ledcSetup(LEDC_CH_BACKLIGHT, LEDC_BL_FREQ, LEDC_BL_RES);
    ledcAttachPin(PIN_BACKLIGHT, LEDC_CH_BACKLIGHT);
    ledcWrite(LEDC_CH_BACKLIGHT, kBrightnessLevels[s_brightIdx]);
}

void refreshAll() {
    if (s_haveNav) {
        tft.fillScreen(s_th.bg);
        updateNavCard(true);
    } else if (s_link) {
        drawIdleScreen("Connected", "Start navigation", "in Google Maps");
    } else {
        drawIdleScreen("AA Nav Display", "Waiting for phone...", "Pair in Bluetooth settings");
    }
}

void pollTouch(uint32_t now) {
    static bool     s_prevPressed = false;
    static uint32_t s_lastTapMs = 0;
    static uint32_t s_pendingSingle = 0;

    uint16_t tx, ty;
    bool pressed = tft.getTouch(&tx, &ty);

    if (pressed && !s_prevPressed) {
        if (s_lastTapMs != 0 && now - s_lastTapMs < DOUBLE_TAP_MS) {
            s_pendingSingle = 0;
            s_night = !s_night;
            s_th = s_night ? kNight : kDay;
            resetLast();
            refreshAll();
        } else {
            s_pendingSingle = now + DOUBLE_TAP_MS;
        }
        s_lastTapMs = now;
    }

    if (s_pendingSingle && now >= s_pendingSingle) {
        s_pendingSingle = 0;
        s_brightIdx = (s_brightIdx + 1) %
                      (sizeof(kBrightnessLevels) / sizeof(kBrightnessLevels[0]));
        applyBrightness();
    }

    s_prevPressed = pressed;
}

}  // namespace

// ----------------------------- Public API ---------------------------------
void uiInit() {
    s_th = kDay;
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(s_th.bg);

    // Default resistive-touch calibration. The 5th element is the rotation
    // the data applies to - it must match setRotation(1) above. Adjust via
    // the TFT_eSPI Touch_calibrate example if taps land offset (see README).
    uint16_t calData[5] = { 275, 3620, 264, 3532, 1 };
    tft.setTouch(calData);

    arrowSpr.setColorDepth(16);
    void* buf = arrowSpr.createSprite(ARROW_PANEL_W, ARROW_PANEL_H);
    s_spriteOk = (buf != nullptr);
    if (!s_spriteOk) {
        Serial.println("[ui] sprite alloc failed - arrow panel disabled");
    }

    applyBrightness();
    resetLast();
    drawIdleScreen("AA Nav Display", "Waiting for phone...", "Pair in Bluetooth settings");
    Serial.printf("[ui] init ok (sprite %s)\n", s_spriteOk ? "on" : "off");
}

void uiSetLink(bool connected) {
    if (s_link == connected) {
        return;
    }
    s_link = connected;
    if (!connected) {
        s_haveNav = false;
        s_nav.clear();
        s_urg = URG_NONE;
        s_sigLost = false;
        s_flashOn = false;
        resetLast();
    }
    refreshAll();
}

void uiSetNav(const NavData& nav) {
    bool wasHaveNav = s_haveNav;
    s_nav = nav;
    s_haveNav = nav.valid;
    s_lastNavMs = millis();

    Urgency urg = urgencyFor(s_nav.d, s_nav.phase);
    if (urg != s_urg) {
        s_urg = urg;
        s_flashOn = (urg == URG_NOW);
        s_lastFlashMs = millis();
    }

    // Progress reference: distance snapshot when the maneuver changes (a
    // much larger distance for the same code also means a new leg).
    if (!wasHaveNav || s_last.m != s_nav.m || s_d0 <= 0 || s_nav.d > s_d0) {
        s_d0 = (s_nav.d > 100) ? s_nav.d : 100;
    }

    if (s_haveNav && !wasHaveNav) {
        // Leaving the idle screen: wipe it once so no idle text survives.
        tft.fillScreen(s_th.bg);
    }
    updateNavCard(false);
}

void uiNoteActivity() {
    s_lastNavMs = millis();
}

void uiTick(uint32_t now) {
    static uint32_t s_lastTouchPoll = 0;
    if (now - s_lastTouchPoll >= TOUCH_POLL_MS) {
        s_lastTouchPoll = now;
        pollTouch(now);
    }

    // TURN NOW flash
    if (s_haveNav && s_nav.phase == PH_NAV && s_urg == URG_NOW &&
        now - s_lastFlashMs >= FLASH_PERIOD_MS) {
        s_lastFlashMs = now;
        s_flashOn = !s_flashOn;
        drawArrowPanel();
        s_last.flash = s_flashOn;
    }

    // Signal-lost banner
    bool lost = s_link && s_haveNav && s_nav.phase == PH_NAV &&
                (now - s_lastNavMs > SIGNAL_LOST_MS);
    if (lost != s_sigLost) {
        s_sigLost = lost;
        if (s_sigLost) {
            drawBanner();
        } else {
            clearBanner();
        }
        s_last.sigLost = s_sigLost;
    }

    pollTouch(now);
}
