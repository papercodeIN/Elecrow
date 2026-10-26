#ifndef IMAGE_DECODER_H
#define IMAGE_DECODER_H

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#if defined(DISPLAY_BACKEND_LOVYANGFX)
#include "display_7inch.h"
#else
#include <TFT_eSPI.h>
#endif
#include <TJpg_Decoder.h>
#include <pngle.h>
#include "config.h"

#if defined(DISPLAY_BACKEND_LOVYANGFX)
extern LGFX tft;
#else
extern TFT_eSPI tft;
#endif
extern SemaphoreHandle_t sdMutex;

enum FitMode {
    FIT_LETTERBOX = 0,  // Scale to fit within the configured panel with black bars
    FIT_CROP = 1,       // Scale to cover the configured panel, crop edges
    FIT_AUTO = 2        // Auto: Fill screen; portrait photos use full height
};

enum TransitionStyle {
    TRANS_INSTANT = 0,
    TRANS_WIPE_RIGHT = 1,
    TRANS_WIPE_LEFT = 2,
    TRANS_WIPE_DOWN = 3,
    TRANS_WIPE_UP = 4,
    TRANS_BLINDS_VERT = 5,
    TRANS_BLINDS_HORIZ = 6,
    TRANS_CURTAIN_OPEN = 7,
    TRANS_CURTAIN_CLOSE = 8,
    TRANS_BOX_IN = 9,
    TRANS_CHECKERBOARD = 10,
    TRANS_FADE = 11,
    TRANS_RANDOM = 12       // Auto: Random effect on every slide
};

// ============================================================
// JPEG Decoder using Bodmer's TJpg_Decoder
// ============================================================
static bool tjpgOutputCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT || (x + w) <= 0 || (y + h) <= 0) {
        return 1;
    }

    if (x >= 0 && (x + w) <= SCREEN_WIDTH && y >= 0 && (y + h) <= SCREEN_HEIGHT) {
        tft.pushImage(x, y, w, h, bitmap);
        return 1;
    }

    // Handle partial clipping at screen edges
    int clipX = x < 0 ? -x : 0;
    int clipY = y < 0 ? -y : 0;
    int drawW = (x + w > SCREEN_WIDTH) ? (SCREEN_WIDTH - x) : (w - clipX);
    int drawH = (y + h > SCREEN_HEIGHT) ? (SCREEN_HEIGHT - y) : (h - clipY);

    if (drawW <= 0 || drawH <= 0) return 1;

    for (int r = 0; r < drawH; r++) {
        uint16_t* src = bitmap + (clipY + r) * w + clipX;
        tft.pushImage(x + clipX, y + clipY + r, drawW, 1, src);
    }

    return 1;
}

static bool renderJPEG(const char* filepath, FitMode fitMode) {
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1500)) != pdTRUE) return false;

    uint16_t origW = 0, origH = 0;
    JRESULT res = TJpgDec.getSdJpgSize(&origW, &origH, filepath);
    if (res != JDR_OK || origW == 0 || origH == 0) {
        if (res == JDR_FMT3) {
            Serial.printf("[JPEG] Not a baseline JPEG (code=8: Progressive JPEG or unsupported subsampling): %s\n", filepath);
            Serial.println("[JPEG] TIP: Upload via the Web Dashboard with auto-convert enabled to normalize to Baseline JPEG.");
        } else {
            Serial.printf("[JPEG] Failed to read header, code=%d\n", res);
        }
        xSemaphoreGive(sdMutex);
        return false;
    }

    // Auto and Crop & Fill cover the complete panel; Fit Whole Photo contains it.
    // TJpgDec supports power-of-two reductions, so select the largest reduction
    // that still covers the panel when filling, or the smallest that contains it.
    bool autoPortrait = (fitMode == FIT_AUTO && origH > origW);
    bool fillMode = (fitMode == FIT_CROP || (fitMode == FIT_AUTO && !autoPortrait));
    uint8_t scale = 1;
    if (fillMode) {
        // Largest supported reduction that still covers the screen.
        while (scale < 8 &&
               (origW / (scale << 1) >= SCREEN_WIDTH || origH / (scale << 1) >= SCREEN_HEIGHT)) {
            scale <<= 1;
        }
    } else {
        // Smallest supported reduction that keeps the whole image within the screen.
        while (scale < 8 &&
               (origW / scale > SCREEN_WIDTH || origH / scale > SCREEN_HEIGHT)) {
            scale <<= 1;
        }
    }

    TJpgDec.setJpgScale(scale);
    TJpgDec.setCallback(tjpgOutputCallback);

    int scaledW = origW / scale;
    int scaledH = origH / scale;
    int x0 = (SCREEN_WIDTH  - scaledW) / 2;
    int y0 = (SCREEN_HEIGHT - scaledH) / 2;
    if (fillMode) {
        // Anchor at the center so any excess is cropped equally on opposing edges.
        x0 = (SCREEN_WIDTH - scaledW) / 2;
        y0 = (SCREEN_HEIGHT - scaledH) / 2;
    }

    tft.setSwapBytes(true);
    tft.fillScreen(TFT_BLACK);
    res = TJpgDec.drawSdJpg(x0, y0, filepath);
    xSemaphoreGive(sdMutex);

    if (res != JDR_OK) {
        if (res == JDR_FMT3) {
            Serial.printf("[JPEG] Render error code=8 (Unsupported JPEG standard / Progressive): %s\n", filepath);
        } else {
            Serial.printf("[JPEG] Render error code=%d\n", res);
        }
        return false;
    }
    return true;
}

// ============================================================
// PNG Decoder using Stream-based pngle
// ============================================================
struct PngRenderContext {
    int x0;
    int y0;
    int curY;
    uint16_t lineBuf[SCREEN_WIDTH];
};

static PngRenderContext g_pngCtx;
static FitMode g_pngFitMode = FIT_AUTO;
static bool g_pngFitWhole = false;
static uint32_t g_pngSourceW = 0;
static uint32_t g_pngSourceH = 0;
static float g_pngScaleX = 1.0f;
static float g_pngScaleY = 1.0f;

static void pngleInitCallback(pngle_t *pngle, uint32_t w, uint32_t h) {
    // Sanity check dimensions
    const int MAX_DIM = 4096;
    if (w == 0 || h == 0 || w > MAX_DIM || h > MAX_DIM) {
        LOG_ERROR("[PNG] Invalid dimensions: %ux%u\n", w, h);
        // Signal error by setting negative dimensions
        g_pngCtx.x0 = -1;
        g_pngCtx.y0 = -1;
        return;
    }
    g_pngSourceW = w;
    g_pngSourceH = h;
    bool autoPortrait = (g_pngFitMode == FIT_AUTO && h > w);
    bool fitWhole = (g_pngFitMode == FIT_LETTERBOX);
    bool autoLandscape = (g_pngFitMode == FIT_AUTO && !autoPortrait);
    bool fillMode = (g_pngFitMode == FIT_CROP || autoLandscape);
    if (fitWhole || autoPortrait || fillMode) {
        float scale = fitWhole
            ? min((float)SCREEN_WIDTH / w, (float)SCREEN_HEIGHT / h)
            : (autoPortrait ? (float)SCREEN_HEIGHT / h
                            : max((float)SCREEN_WIDTH / w, (float)SCREEN_HEIGHT / h));
        int targetW = (int)(w * scale);
        int targetH = (int)(h * scale);
        g_pngScaleX = (float)targetW / w;
        g_pngScaleY = (float)targetH / h;
        g_pngCtx.x0 = (SCREEN_WIDTH - targetW) / 2;
        g_pngCtx.y0 = (SCREEN_HEIGHT - targetH) / 2;
    } else {
        g_pngScaleX = g_pngScaleY = 1.0f;
        g_pngCtx.x0 = (SCREEN_WIDTH - (int)w) / 2;
        g_pngCtx.y0 = (SCREEN_HEIGHT - (int)h) / 2;
    }
    g_pngCtx.curY = -1;
}

static void pngleDrawCallback(pngle_t *pngle, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint8_t rgba[4]) {
    // Skip if init callback detected invalid dimensions
    if (g_pngCtx.x0 < 0 || g_pngCtx.y0 < 0) return;
    
    if (rgba[3] == 0) return; // Transparent pixel

    if (g_pngFitWhole || g_pngFitMode == FIT_AUTO || g_pngFitMode == FIT_CROP) {
        int left = (int)(x * g_pngScaleX) + g_pngCtx.x0;
        int top = (int)(y * g_pngScaleY) + g_pngCtx.y0;
        int right = (int)((x + w) * g_pngScaleX) + g_pngCtx.x0;
        int bottom = (int)((y + h) * g_pngScaleY) + g_pngCtx.y0;
        int x0 = max(0, left), y0 = max(0, top);
        int x1 = min(SCREEN_WIDTH, right), y1 = min(SCREEN_HEIGHT, bottom);
        if (x1 <= x0 || y1 <= y0) return;
        uint16_t color = tft.color565(rgba[0], rgba[1], rgba[2]);
        tft.fillRect(x0, y0, x1 - x0, y1 - y0, color);
        return;
    }

    int screenX = (int)x + g_pngCtx.x0;
    int screenY = (int)y + g_pngCtx.y0;
    if (screenX >= 0 && screenX < SCREEN_WIDTH && screenY >= 0 && screenY < SCREEN_HEIGHT) {
        uint16_t color = tft.color565(rgba[0], rgba[1], rgba[2]);
        if (w == 1 && h == 1) tft.drawPixel(screenX, screenY, color);
        else tft.fillRect(screenX, screenY, w, h, color);
    }
}

static bool renderPNG(const char* filepath, FitMode fitMode) {
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1500)) != pdTRUE) return false;

    File file = SD.open(filepath, FILE_READ);
    if (!file) {
        xSemaphoreGive(sdMutex);
        return false;
    }

    pngle_t *pngle = pngle_new();
    if (!pngle) {
        file.close();
        xSemaphoreGive(sdMutex);
        return false;
    }

    g_pngFitMode = fitMode;
    g_pngFitWhole = (fitMode == FIT_LETTERBOX);
    g_pngSourceW = g_pngSourceH = 0;
    pngle_set_init_callback(pngle, pngleInitCallback);
    pngle_set_draw_callback(pngle, pngleDrawCallback);

    tft.fillScreen(TFT_BLACK);

    tft.setSwapBytes(false);

    uint8_t buffer[512];
    bool ok = true;
    while (file.available()) {
        int bytesRead = file.read(buffer, sizeof(buffer));
        if (bytesRead <= 0) break;

        int fed = pngle_feed(pngle, buffer, bytesRead);
        if (fed < 0) {
            Serial.printf("[PNGLE] Decode failed: %s\n", pngle_error(pngle));
            ok = false;
            break;
        }
    }

    // Check if init callback detected invalid dimensions
    if (g_pngCtx.x0 < 0 || g_pngCtx.y0 < 0) {
        ok = false;
    }

    pngle_destroy(pngle);
    file.close();
    xSemaphoreGive(sdMutex);
    return ok;
}

// ============================================================
// Native Streaming BMP Decoder
// ============================================================
static bool renderBMP(const char* path, FitMode fitMode) {
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1500)) != pdTRUE) return false;

    File file = SD.open(path, FILE_READ);
    if (!file) {
        xSemaphoreGive(sdMutex);
        return false;
    }

    uint8_t header[54];
    if (file.read(header, 54) != 54) {
        file.close();
        xSemaphoreGive(sdMutex);
        return false;
    }

    // Validate 'BM' signature
    if (header[0] != 0x42 || header[1] != 0x4D) {
        file.close();
        xSemaphoreGive(sdMutex);
        return false;
    }

    uint32_t dataOffset = header[10] | (header[11] << 8) | (header[12] << 16) | (header[13] << 24);
    int32_t width       = header[18] | (header[19] << 8) | (header[20] << 16) | (header[21] << 24);
    int32_t height      = header[22] | (header[23] << 8) | (header[24] << 16) | (header[25] << 24);
    uint16_t bpp        = header[28] | (header[29] << 8);
    uint32_t comp       = header[30] | (header[31] << 8) | (header[32] << 16) | (header[33] << 24);

    // Sanity checks to prevent malicious files from causing issues
    const int MAX_DIM = 4096;
    if (width <= 0 || height <= 0 || width > MAX_DIM || height > MAX_DIM) {
        LOG_ERROR("[BMP] Invalid dimensions: %dx%d\n", width, height);
        file.close();
        xSemaphoreGive(sdMutex);
        return false;
    }

    if (bpp != 24 && bpp != 32 && bpp != 16) {
        file.close();
        xSemaphoreGive(sdMutex);
        return false;
    }

    if (comp != 0 && comp != 3) {
        file.close();
        xSemaphoreGive(sdMutex);
        return false;
    }

    bool bottomUp = true;
    if (height < 0) {
        bottomUp = false;
        height = -height;
    }

    int bytesPerPixel = bpp / 8;
    int rowStride = ((width * bytesPerPixel + 3) / 4) * 4;

    // Prevent excessive rowStride allocation
    if (rowStride > MAX_DIM * 4) {
        LOG_ERROR("[BMP] Row stride too large: %d\n", rowStride);
        file.close();
        xSemaphoreGive(sdMutex);
        return false;
    }

    bool autoPortrait = (fitMode == FIT_AUTO && height > width);
    bool autoLandscape = (fitMode == FIT_AUTO && !autoPortrait);
    bool fillMode = (fitMode == FIT_CROP || autoLandscape);
    float scale = fillMode
        ? max((float)SCREEN_WIDTH / width, (float)SCREEN_HEIGHT / height)
        : min((float)SCREEN_WIDTH / width, (float)SCREEN_HEIGHT / height);
    int displayW = max(1, (int)(width * scale));
    int displayH = max(1, (int)(height * scale));
    int xOffset = (SCREEN_WIDTH - displayW) / 2;
    int yOffset = (SCREEN_HEIGHT - displayH) / 2;
    float scaleX = (float)displayW / width;
    float scaleY = (float)displayH / height;
    tft.fillScreen(TFT_BLACK);

    tft.setSwapBytes(false);

    uint8_t* rowBuf = (uint8_t*)malloc(rowStride);
    if (!rowBuf) {
        file.close();
        xSemaphoreGive(sdMutex);
        return false;
    }

    static uint16_t pixelLine[SCREEN_WIDTH];

    for (int row = 0; row < height; row++) {
        int imgY = bottomUp ? (height - 1 - row) : row;
        int screenY = yOffset + (int)(imgY * scaleY);
        int rowEndY = yOffset + (int)((imgY + 1) * scaleY);
        if (screenY < 0 || screenY >= SCREEN_HEIGHT) continue;
        if (rowEndY > SCREEN_HEIGHT) rowEndY = SCREEN_HEIGHT;

        uint32_t rowPos = dataOffset + (uint32_t)row * rowStride;
        file.seek(rowPos);
        if (file.read(rowBuf, rowStride) != (size_t)rowStride) break;

        int startX = max(0, xOffset);
        int endX = min(SCREEN_WIDTH, xOffset + displayW);
        int drawW = endX - startX;
        if (drawW <= 0) continue;

        for (int col = 0; col < drawW; col++) {
            int sourceX = min(width - 1, max(0, (int)((startX + col - xOffset) / scaleX)));
            int srcIdx = sourceX * bytesPerPixel;
            uint16_t color;
            if (bytesPerPixel >= 3) {
                uint8_t b = rowBuf[srcIdx];
                uint8_t g = rowBuf[srcIdx + 1];
                uint8_t r = rowBuf[srcIdx + 2];
                color = tft.color565(r, g, b);
            } else {
                color = rowBuf[srcIdx] | (rowBuf[srcIdx + 1] << 8);
            }
            pixelLine[col] = color;
        }

        if (rowEndY > SCREEN_HEIGHT) rowEndY = SCREEN_HEIGHT;
        for (int drawY = max(0, screenY); drawY < rowEndY; drawY++) {
            tft.pushImage(startX, drawY, drawW, 1, pixelLine);
        }
    }

    free(rowBuf);
    file.close();
    xSemaphoreGive(sdMutex);
    return true;
}

// ============================================================
// Master Render Dispatcher
// ============================================================
static bool renderImageFile(const char* filepath, FitMode fitMode, TransitionStyle trans) {
    String pathStr = String(filepath);
    pathStr.toLowerCase();

    // Transition handling moved to main.cpp (non-blocking state machine)
    // This function now only handles image decoding/rendering

    if (pathStr.endsWith(".jpg") || pathStr.endsWith(".jpeg")) {
        return renderJPEG(filepath, fitMode);
    } else if (pathStr.endsWith(".png")) {
        return renderPNG(filepath, fitMode);
    } else if (pathStr.endsWith(".bmp")) {
        return renderBMP(filepath, fitMode);
    }

    Serial.printf("[DISPLAY] Unsupported image format: %s\n", filepath);
    return false;
}

#endif // IMAGE_DECODER_H

