// CrowPanel 7" ESP32-S3 - Bluetooth Stream Deck (4x4 tile grid over BLE HID)
// Display: 800x480 RGB LCD, LVGL partial refresh (dirty-rect blit into the live framebuffer)
// Touch:   GT911 via direct I2C (0x5D or 0x14)
// Input:   BLE HID keyboard - sends key/media reports directly to the paired host
//
// Identity: the ESP32's factory-burned public address. Bonding requires the
// advertised address and the SMP identity address to be the same; Bluedroid
// always distributes the public address, so overriding the controller address
// with a random static one (an earlier attempt at "re-pairing hygiene") made
// every pairing fail with SMP_CONN_TOUT and Windows then connect/disconnect
// in a loop. Bonds persist in NVS across reboots (CONFIG_BT_SMP_ENABLE).
// If Windows ever holds a pairing the deck no longer recognises, remove the
// device in Windows Bluetooth settings once and pair again - the firmware
// never erases bonds or changes its address on its own.

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <BleKeyboard.h>
#include <BLEDevice.h>   // ESP-IDF BLE + bond APIs (diagnostics logging)
#include <lvgl.h>

// Set to 1 in platformio.ini build_flags if touch stays dead or reads garbage
// on a V3.0 board: forces a GT911 reset through the PCA9557 on every boot.
#ifndef FORCE_TOUCH_RESET
#define FORCE_TOUCH_RESET 0
#endif

// ---------- BLE connection state ----------
static constexpr uint32_t HID_CHORD_MS = 80;           // key-down hold for modifier chords
static constexpr uint32_t TERMINAL_LAUNCH_MS = 500;    // wait for Win+R menu before typing
static constexpr uint32_t BLE_POLL_MS = 500;            // connection-state poll interval
static constexpr uint32_t BLE_INIT_DELAY_MS = 3000;    // allow BLE stack + host pairing time

static constexpr size_t TAP_QUEUE_LEN = 8;

// ---------- Pins ----------
#define TFT_BL 2
#define TFT_VSYNC 40
#define TOUCH_SDA 19
#define TOUCH_SCL 20

// ---------- Display ----------
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    41, 40, 39, 0,     // VSYNC, HSYNC, DEN, CLK (CLK=0 = use default)
    14, 21, 47, 48, 45,// R, G, B data pins
    9,  46, 3,  8,  16, 1, // more RGB data
    15, 7,  6,  5,  4,    // remaining data/control
    0, TFT_VSYNC, 48, 40,  // TFT_VSYNC passed as vsync pin
    0, 1,  31, 13,         // clock/reset config
    1, 15000000);           // enable, freq

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(800, 480, rgbpanel, 0, true);

// ---------- Touch (GT911 direct I2C) ----------
// CrowPanel V3.0 routes GT911 RST/INT through a PCA9557 I2C expander at 0x18
// (bit0 = RST, bit1 = INT). V1/V2 wire them straight to the ESP32, so the
// expander is only touched as a fallback when the GT911 doesn't answer.
static constexpr uint8_t PCA9557_ADDR = 0x18;
static constexpr uint8_t PCA9557_OUTPUT = 0x01;
static constexpr uint8_t PCA9557_CONFIG = 0x03;

static uint8_t gt911_addr = 0x5D;
static constexpr uint16_t GT911_REG_STATUS = 0x814E;
static constexpr uint16_t GT911_REG_POINT1 = 0x8150;

static bool gt911_probe(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

static bool gt911_read(uint16_t reg, uint8_t *buf, size_t n) {
    Wire.beginTransmission(gt911_addr);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    if (Wire.endTransmission(false) != 0) return false;
    size_t got = Wire.requestFrom((int)gt911_addr, (int)n);
    if (got != n) return false;
    for (size_t i = 0; i < n; i++) buf[i] = Wire.read();
    return true;
}

static void gt911_write(uint16_t reg, uint8_t val) {
    Wire.beginTransmission(gt911_addr);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    Wire.write(val);
    Wire.endTransmission();
}

static bool gt911_read_point(uint16_t &x, uint16_t &y) {
    uint8_t status;
    if (!gt911_read(GT911_REG_STATUS, &status, 1)) return false;
    if ((status & 0x80) == 0) return false;
    uint8_t touches = status & 0x0F;
    bool has = false;
    if (touches > 0) {
        uint8_t p[4];
        if (gt911_read(GT911_REG_POINT1, p, 4)) {
            x = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
            y = (uint16_t)p[2] | ((uint16_t)p[3] << 8);
            has = true;
        }
    }
    gt911_write(GT911_REG_STATUS, 0);
    return has;
}

static void pca9557_write(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(PCA9557_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

// Pulse GT911 reset with INT low so the controller comes up on 0x5D.
static void gt911_reset_via_expander() {
    pca9557_write(PCA9557_CONFIG, 0x00);  // all pins as outputs
    pca9557_write(PCA9557_OUTPUT, 0x00);  // RST low, INT low
    delay(20);
    pca9557_write(PCA9557_OUTPUT, 0x01);  // RST high, INT low -> addr 0x5D
    delay(10);
    pca9557_write(PCA9557_OUTPUT, 0x03);  // release INT
    delay(150);
}

// ---------- LVGL ----------
static const uint32_t SCREEN_W = 800;
static const uint32_t SCREEN_H = 480;
static const uint32_t DRAW_BUF_LINES = 48;  // ~1/10 screen per render chunk
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *lvgl_buf;
static SemaphoreHandle_t lvgl_mux;
static QueueHandle_t tap_queue = nullptr;

// Idle dimmer state (used by the LVGL task and the touch callback).
static uint32_t last_input_ms = 0;
static bool dimmed = false;
static constexpr uint32_t DIM_AFTER_MS = 60000;  // dim after 1 min untouched

// The panel dims by lowering the backlight PWM, not by painting a translucent
// sheet. A full-screen sheet forces a 10-chunk repaint of the whole framebuffer
// the moment it is removed, which starves the RGB scanout and leaves the panel
// black. PWM costs nothing and needs no repaint at all.
static constexpr uint8_t BL_CHANNEL = 0;
static constexpr uint32_t BL_FREQ_HZ = 20000;
static constexpr uint8_t BL_RES_BITS = 8;
static constexpr uint8_t BL_ON_DUTY = 255;
static constexpr uint8_t BL_DIM_DUTY = 40;  // ~16 %

static void backlight_init() {
    ledcSetup(BL_CHANNEL, BL_FREQ_HZ, BL_RES_BITS);
    ledcAttachPin(TFT_BL, BL_CHANNEL);
    ledcWrite(BL_CHANNEL, BL_ON_DUTY);
}

static void backlight_set(bool on) {
    ledcWrite(BL_CHANNEL, on ? BL_ON_DUTY : BL_DIM_DUTY);
}

// LVGL is not thread-safe. Every widget touch must hold lvgl_mux, except when
// the caller already owns it (LVGL runs its own timer task).
static bool ui_lock() {
    if (!lvgl_mux) return false;
    if (xSemaphoreGetMutexHolder(lvgl_mux) == xTaskGetCurrentTaskHandle()) return false;
    return xSemaphoreTake(lvgl_mux, portMAX_DELAY) == pdTRUE;
}

static void ui_unlock(bool locked) {
    if (locked) xSemaphoreGive(lvgl_mux);
}

// Partial refresh: blit only the invalidated rectangle into the live framebuffer.
// A full-screen 768 KB copy per tap used to tear visibly while the panel scanned
// it out; a button-sized blit keeps the rest of the screen untouched.
//
// The panel scans the framebuffer continuously, so a blit that lands while the
// beam crosses the same rows shows up as a flicker. Each refresh therefore starts
// right after VSYNC: LVGL then paints top-to-bottom faster than the beam moves,
// so the writes stay ahead of it.
static volatile uint32_t vsync_seq = 0;
static bool vsync_sync = false;
static bool refresh_pending = true;

static void IRAM_ATTR on_rgb_vsync() {
    vsync_seq++;
}

static bool vblank() {
    const uint32_t seq = vsync_seq;
    const uint32_t started = millis();
    while (vsync_seq == seq) {
        if (millis() - started >= 50) return false;
        vTaskDelay(1);
    }
    return true;
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    if (refresh_pending && vsync_sync) {
        if (!vblank()) vsync_sync = false;  // panel stopped signalling: never stall again
    }
    const int16_t w = (int16_t)(area->x2 - area->x1 + 1);
    const int16_t h = (int16_t)(area->y2 - area->y1 + 1);
    gfx->draw16bitRGBBitmap((int16_t)area->x1, (int16_t)area->y1, (uint16_t *)color_p, w, h);
    refresh_pending = lv_disp_flush_is_last(drv);
    lv_disp_flush_ready(drv);
}

static void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    static uint16_t last_x = 0, last_y = 0;
    static bool pressed = false;
    uint16_t x, y;
    if (gt911_read_point(x, y)) {
        last_x = x; last_y = y; pressed = true;
        last_input_ms = millis();
    } else {
        pressed = false;
    }
    data->point.x = last_x;
    data->point.y = last_y;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void lvgl_task(void *arg) {
    (void)arg;
    while (true) {
        if (xSemaphoreTake(lvgl_mux, portMAX_DELAY) == pdTRUE) {
            lv_timer_handler();
            const bool idle = (uint32_t)(millis() - last_input_ms) > DIM_AFTER_MS;
            if (idle != dimmed) {
                dimmed = idle;
                backlight_set(!idle);
                Serial.println(idle ? "backlight: dim" : "backlight: full");
            }
            xSemaphoreGive(lvgl_mux);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// ---------- Actions ----------

// Palette: near-black green background, deep green cards, lime accent,
// teal secondary, sage muted text, amber for launchers, red for destructive.
static constexpr uint32_t C_BG      = 0x030906;
static constexpr uint32_t C_CARD    = 0x0B2417;
static constexpr uint32_t C_LIME    = 0x4ADE4A;
static constexpr uint32_t C_TEAL    = 0x3393E0;
static constexpr uint32_t C_SAGE    = 0x84A18C;
static constexpr uint32_t C_MINT    = 0xD3ECD8;
static constexpr uint32_t C_RED     = 0xFB2C36;
static constexpr uint32_t C_AMBER   = 0xFCBB00;

struct Tile {
    const char *key;        // id used in send_action()
    const char *label;      // shown on tile
    const char *icon;       // LVGL symbol
    uint32_t    accent;     // accent color
};

static const Tile TILES[] = {
    { "mute",          "MUTE",       LV_SYMBOL_MUTE,        C_LIME  },
    { "play_pause",    "PLAY/PAUSE", LV_SYMBOL_PLAY,        C_LIME  },
    { "next_track",    "NEXT",       LV_SYMBOL_NEXT,        C_LIME  },
    { "vol_down",      "VOL DOWN",   LV_SYMBOL_VOLUME_MID,  C_TEAL  },
    { "vol_up",        "VOL UP",     LV_SYMBOL_VOLUME_MAX,  C_TEAL  },
    { "mic_mute",      "MIC",        LV_SYMBOL_AUDIO,       C_RED   },
    { "esc",           "ESC",        LV_SYMBOL_CLOSE,       C_SAGE  },
    { "snip",          "SNIP",       LV_SYMBOL_IMAGE,       C_AMBER },
    { "copy",          "COPY",       LV_SYMBOL_COPY,        C_TEAL  },
    { "paste",         "PASTE",      LV_SYMBOL_PASTE,       C_TEAL  },
    { "show_desktop",  "DESKTOP",    LV_SYMBOL_HOME,        C_SAGE  },
    { "open_terminal", "TERMINAL",   LV_SYMBOL_KEYBOARD,    C_AMBER },
    { "lock_screen",   "LOCK",       LV_SYMBOL_CLOSE,       C_RED   },
    { "start",         "START",      LV_SYMBOL_BARS,        C_LIME  },
    { "alt_tab",       "ALT+TAB",    LV_SYMBOL_LIST,        C_SAGE  },
    { "ctrl_tab",      "CTRL+TAB",   LV_SYMBOL_RIGHT,       C_SAGE  },
};

static constexpr int TILE_COUNT = (int)(sizeof(TILES) / sizeof(TILES[0]));
static_assert(TILE_COUNT == 16, "The deck layout is a fixed 4x4 grid; update build_deck_layer() too.");

// ---------- Status UI ----------
static lv_obj_t *status_label = nullptr;
static lv_obj_t *pair_status_label = nullptr;
static lv_obj_t *layer_deck = nullptr;

static void set_status(const char *msg, uint32_t color = C_SAGE) {
    if (!status_label) return;
    bool locked = ui_lock();
    lv_label_set_text(status_label, msg);
    lv_obj_set_style_text_color(status_label, lv_color_hex(color), 0);
    ui_unlock(locked);
}

static void set_pair_status(const char *msg, uint32_t color = C_SAGE) {
    if (!pair_status_label) return;
    bool locked = ui_lock();
    lv_label_set_text(pair_status_label, msg);
    lv_obj_set_style_text_color(pair_status_label, lv_color_hex(color), 0);
    ui_unlock(locked);
}

// ---------- BLE ----------

// Chained after the library's own GAP handler. Logs the events that decide
// whether the deck is actually discoverable and pairable.
static void gap_log_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        Serial.printf("BLE: advertising %s (status %d)\n",
                      param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS ? "running" : "FAILED",
                      (int)param->adv_start_cmpl.status);
        break;
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        Serial.printf("BLE: pairing %s (fail reason %d, peer addr type %d)\n",
                      param->ble_security.auth_cmpl.success ? "ok" : "FAILED",
                      (int)param->ble_security.auth_cmpl.fail_reason,
                      (int)param->ble_security.auth_cmpl.addr_type);
        break;
    default:
        break;
    }
}

// Fast reconnect: set the preferred advertising interval so hosts find the
// device quickly. We use standard HID-friendly values.
class DeckBleKeyboard : public BleKeyboard {
public:
    DeckBleKeyboard(const std::string &name, const std::string &manufacturer, uint8_t battery)
        : BleKeyboard(name, manufacturer, battery) {}

    void onConnect(BLEServer *pServer) override {
        BleKeyboard::onConnect(pServer);
        Serial.println("BLE callback: GATT connected; HID notifications enabled");
    }

    void onDisconnect(BLEServer *pServer) override {
        BleKeyboard::onDisconnect(pServer);
        Serial.println("BLE callback: GATT disconnected; advertising restarted");
    }

    void onStarted(BLEServer *pServer) override {
        Serial.println("BLE: configuring advertising as BT Stream Deck");
        // The library defaults to ESP_LE_AUTH_REQ_SC_MITM_BOND. MITM is
        // impossible here (no display, no keypad for a passkey), and a MITM
        // requirement against a NoInputNoOutput device is what makes Windows
        // abort pairing and cycle connect/disconnect. Bonding without MITM
        // (Just Works) pairs cleanly and the bond persists in NVS.
        uint8_t auth_req = ESP_LE_AUTH_BOND;
        esp_err_t sec_err = esp_ble_gap_set_security_param(
            ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(auth_req));
        uint8_t io_cap = ESP_IO_CAP_NONE;
        esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &io_cap, sizeof(io_cap));
        Serial.printf("BLE: bonding security config: %s\n", esp_err_to_name(sec_err));

        BLEAdvertising *adv = pServer->getAdvertising();
        // Advertising interval: 31.25-46.875 ms (in 0.625ms units: 0x20 = 50 -> 31.25ms, 0x30 = 75 -> 46.875ms)
        adv->setMinInterval(0x20);
        adv->setMaxInterval(0x30);
        // Preferred connection interval: 15-30 ms (in 1.25ms units: 0x0C = 15ms, 0x18 = 30ms)
        adv->setMinPreferred(0x0C);
        adv->setMaxPreferred(0x18);
    }
};

static DeckBleKeyboard bleKeyboard("BT Stream Deck", "Elecrow", 100);

// Request from the LVGL tap callback to the HID task.
struct TapRequest {
    const Tile *tile;
};

// Forward declarations for tap handling (used by build_deck_layer and hid_task).
static void tile_clicked_cb(lv_event_t *e);

enum class ActionStatus {
    Sent,
    NotConnected,
    UnknownKey,
};

static ActionStatus send_hotkey(uint8_t modifier, uint8_t key) {
    bleKeyboard.press(modifier);
    bleKeyboard.press(key);
    vTaskDelay(pdMS_TO_TICKS(HID_CHORD_MS));
    bleKeyboard.releaseAll();
    return ActionStatus::Sent;
}

static ActionStatus send_action(const char *key) {
    if (!key || !*key) return ActionStatus::UnknownKey;
    if (!bleKeyboard.isConnected()) return ActionStatus::NotConnected;

    if (strcmp(key, "mute") == 0)          bleKeyboard.write(KEY_MEDIA_MUTE);
    else if (strcmp(key, "play_pause") == 0)  bleKeyboard.write(KEY_MEDIA_PLAY_PAUSE);
    else if (strcmp(key, "next_track") == 0)  bleKeyboard.write(KEY_MEDIA_NEXT_TRACK);
    else if (strcmp(key, "vol_down") == 0)  bleKeyboard.write(KEY_MEDIA_VOLUME_DOWN);
    else if (strcmp(key, "vol_up") == 0)    bleKeyboard.write(KEY_MEDIA_VOLUME_UP);
    else if (strcmp(key, "mic_mute") == 0)  send_hotkey(KEY_LEFT_CTRL, 'd');
    else if (strcmp(key, "esc") == 0)       bleKeyboard.write(KEY_ESC);
    else if (strcmp(key, "snip") == 0) {
        bleKeyboard.press(KEY_LEFT_GUI);
        bleKeyboard.press(KEY_LEFT_SHIFT);
        bleKeyboard.press('s');
        vTaskDelay(pdMS_TO_TICKS(HID_CHORD_MS));
        bleKeyboard.releaseAll();
    } else if (strcmp(key, "copy") == 0)    send_hotkey(KEY_LEFT_CTRL, 'c');
    else if (strcmp(key, "paste") == 0)     send_hotkey(KEY_LEFT_CTRL, 'v');
    else if (strcmp(key, "show_desktop") == 0) send_hotkey(KEY_LEFT_GUI, 'd');
    else if (strcmp(key, "open_terminal") == 0) {
        send_hotkey(KEY_LEFT_GUI, 'r');
        vTaskDelay(pdMS_TO_TICKS(TERMINAL_LAUNCH_MS));
        bleKeyboard.print("wt");
        bleKeyboard.write(KEY_RETURN);
    } else if (strcmp(key, "lock_screen") == 0) send_hotkey(KEY_LEFT_GUI, 'l');
    else if (strcmp(key, "start") == 0) {
        bleKeyboard.press(KEY_LEFT_GUI);
        vTaskDelay(pdMS_TO_TICKS(HID_CHORD_MS));
        bleKeyboard.releaseAll();
    } else if (strcmp(key, "alt_tab") == 0) send_hotkey(KEY_LEFT_ALT, KEY_TAB);
    else if (strcmp(key, "ctrl_tab") == 0)  send_hotkey(KEY_LEFT_CTRL, KEY_TAB);
    else return ActionStatus::UnknownKey;

    Serial.printf("HID action: %s\n", key);
    return ActionStatus::Sent;
}

// ------- UI building functions -------

static void build_header(lv_obj_t *scr) {
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_size(header, SCREEN_W, 44);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(C_BG), 0);
    lv_obj_set_style_border_width(header, 2, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(C_LIME), 0);
    lv_obj_set_style_border_opa(header, LV_OPA_40, 0);
    lv_obj_set_style_pad_all(header, 4, 0);
    lv_obj_set_scrollbar_mode(header, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Bluetooth Stream Deck");
    lv_obj_set_style_text_color(title, lv_color_hex(C_MINT), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, 0);

    status_label = lv_label_create(header);
    lv_label_set_text(status_label, "booting...");
    lv_obj_set_style_text_color(status_label, lv_color_hex(C_SAGE), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    pair_status_label = lv_label_create(header);
    lv_label_set_text(pair_status_label, "awaiting pairing");
    lv_obj_set_style_text_color(pair_status_label, lv_color_hex(C_AMBER), 0);
    lv_obj_set_style_text_font(pair_status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(pair_status_label, LV_ALIGN_TOP_RIGHT, 0, 0);
}

static void build_deck_layer(lv_obj_t *parent) {
    layer_deck = lv_obj_create(parent);
    lv_obj_set_size(layer_deck, SCREEN_W, SCREEN_H - 44);
    lv_obj_align(layer_deck, LV_ALIGN_TOP_MID, 0, 44);
    lv_obj_set_style_bg_color(layer_deck, lv_color_hex(C_BG), 0);
    lv_obj_set_style_border_width(layer_deck, 0, 0);
    lv_obj_set_style_pad_all(layer_deck, 0, 0);
    lv_obj_set_scrollbar_mode(layer_deck, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(layer_deck, LV_OBJ_FLAG_SCROLLABLE);

    const int GAP = 12;
    const int MARGIN_X = 12;
    const int INNER_TOP = 8;
    const int INNER_BOT = 8;
    const int COLS = 4;
    const int ROWS = 4;
    const int inner_h = (SCREEN_H - 44);
    const int TILE_W = (SCREEN_W - 2 * MARGIN_X - (COLS - 1) * GAP) / COLS;
    const int TILE_H = (inner_h - INNER_TOP - INNER_BOT - (ROWS - 1) * GAP) / ROWS;

    // Faint technical grid behind the keys, static so it costs one draw.
    for (int x = 0; x < SCREEN_W; x += 40) {
        lv_obj_t *ln = lv_obj_create(layer_deck);
        lv_obj_remove_style_all(ln);
        lv_obj_set_size(ln, 1, inner_h);
        lv_obj_set_pos(ln, x, 0);
        lv_obj_set_style_bg_color(ln, lv_color_hex(C_CARD), 0);
        lv_obj_set_style_bg_opa(ln, LV_OPA_50, 0);
        lv_obj_clear_flag(ln, LV_OBJ_FLAG_CLICKABLE);
    }
    for (int y = 0; y < inner_h; y += 40) {
        lv_obj_t *ln = lv_obj_create(layer_deck);
        lv_obj_remove_style_all(ln);
        lv_obj_set_size(ln, SCREEN_W, 1);
        lv_obj_set_pos(ln, 0, y);
        lv_obj_set_style_bg_color(ln, lv_color_hex(C_CARD), 0);
        lv_obj_set_style_bg_opa(ln, LV_OPA_50, 0);
        lv_obj_clear_flag(ln, LV_OBJ_FLAG_CLICKABLE);
    }

    for (int i = 0; i < TILE_COUNT; i++) {
        int col = i % COLS;
        int row = i / COLS;
        const Tile *t = &TILES[i];

        lv_obj_t *tile = lv_btn_create(layer_deck);
        lv_obj_set_size(tile, TILE_W, TILE_H);
        lv_obj_set_pos(tile,
                       MARGIN_X + col * (TILE_W + GAP),
                       INNER_TOP + row * (TILE_H + GAP));

        // Tech panel: flat near-black green, 1 px keyline in the key's colour
        // and a soft glow.
        lv_obj_set_style_bg_color(tile, lv_color_hex(C_CARD), 0);
        lv_obj_set_style_bg_grad_dir(tile, LV_GRAD_DIR_NONE, 0);
        lv_obj_set_style_bg_color(tile, lv_color_hex(0x143D28), LV_STATE_PRESSED);
        lv_obj_set_style_radius(tile, 10, 0);
        lv_obj_set_style_border_width(tile, 1, 0);
        lv_obj_set_style_border_color(tile, lv_color_hex(t->accent), 0);
        lv_obj_set_style_border_opa(tile, LV_OPA_40, 0);
        lv_obj_set_style_border_opa(tile, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_shadow_color(tile, lv_color_hex(t->accent), 0);
        lv_obj_set_style_shadow_width(tile, 10, 0);
        lv_obj_set_style_shadow_spread(tile, 0, 0);
        lv_obj_set_style_shadow_opa(tile, LV_OPA_20, 0);
        lv_obj_set_style_shadow_width(tile, 18, LV_STATE_PRESSED);
        lv_obj_set_style_shadow_opa(tile, LV_OPA_50, LV_STATE_PRESSED);
        lv_obj_set_style_pad_all(tile, 0, 0);

        // Slim glowing rail on the left edge, like an indicator LED.
        lv_obj_t *accent = lv_obj_create(tile);
        lv_obj_set_size(accent, 4, TILE_H - 24);
        lv_obj_align(accent, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_color(accent, lv_color_hex(t->accent), 0);
        lv_obj_set_style_border_width(accent, 0, 0);
        lv_obj_set_style_radius(accent, 2, 0);
        lv_obj_set_style_shadow_color(accent, lv_color_hex(t->accent), 0);
        lv_obj_set_style_shadow_width(accent, 8, 0);
        lv_obj_set_style_shadow_opa(accent, LV_OPA_40, 0);
        lv_obj_set_scrollbar_mode(accent, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(accent, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *icon = lv_label_create(tile);
        lv_label_set_text(icon, t->icon);
        lv_obj_set_style_text_color(icon, lv_color_hex(t->accent), 0);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_32, 0);
        lv_obj_align(icon, LV_ALIGN_CENTER, 0, -16);

        lv_obj_t *lab = lv_label_create(tile);
        lv_label_set_text(lab, t->label);
        lv_obj_set_style_text_color(lab, lv_color_hex(C_MINT), 0);
        lv_obj_set_style_text_letter_space(lab, 1, 0);
        lv_obj_set_style_text_font(lab, &lv_font_montserrat_16, 0);
        lv_obj_align(lab, LV_ALIGN_CENTER, 0, 24);

        lv_obj_add_event_cb(tile, tile_clicked_cb, LV_EVENT_CLICKED, (void *)t);
    }
}

static void build_ui() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    build_header(scr);
    build_deck_layer(scr);
}

// ---------- Tap handling ----------

// Tap callback: LVGL task context. Must not block, so it only enqueues.
static void tile_clicked_cb(lv_event_t *e) {
    const Tile *t = (const Tile *)lv_event_get_user_data(e);
    if (!t) return;
    Serial.printf("tap: %s\n", t->key);

    TapRequest req = { t };
    if (xQueueSend(tap_queue, &req, 0) != pdTRUE) {
        Serial.printf("tap queue full, dropped %s\n", t->key);
        set_status("busy", C_AMBER);
    }
}

static void hid_task(void *arg) {
    (void)arg;
    TapRequest req;
    for (;;) {
        if (xQueueReceive(tap_queue, &req, portMAX_DELAY) == pdTRUE) {
            if (req.tile) {
                switch (send_action(req.tile->key)) {
                    case ActionStatus::Sent:
                        set_status(req.tile->label, C_LIME);
                        break;
                    case ActionStatus::NotConnected:
                        set_status("not connected", C_RED);
                        break;
                    case ActionStatus::UnknownKey:
                        set_status("unknown action", C_AMBER);
                        break;
                }
            }
        }
    }
}

// BLE connection monitor tracks connection/disconnection state and updates
// the header status. Runs after a short delay so the BLE stack has time to
// initialize and the host has time to pair after the deck advertises.
static void connection_task(void *arg) {
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(BLE_INIT_DELAY_MS));
    bool first = true;
    bool previous = false;
    for (;;) {
        const bool connected = bleKeyboard.isConnected();
        if (first || connected != previous) {
            first = false;
            previous = connected;
            Serial.printf("BLE %s\n", connected ? "connected" : "disconnected");
            if (connected) {
                set_status("Bluetooth ready", C_LIME);
                set_pair_status("Connected to computer", C_LIME);
            } else {
                set_status("awaiting pairing", C_AMBER);
                set_pair_status("Pair from Windows Bluetooth settings", C_SAGE);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(BLE_POLL_MS));
    }
}

static void halt_with_screen(const char *msg) {
    Serial.println(msg);
    gfx->fillScreen(BLACK);
    gfx->setTextColor(gfx->color565(0xEF, 0x44, 0x44));
    gfx->setTextSize(2);
    gfx->setCursor(20, 20);
    gfx->print(msg);
    while (true) delay(1000);
}

// ---------- Setup ----------
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== Bluetooth Stream Deck boot ===");

    backlight_init();

    if (!gfx->begin()) {
        Serial.println("gfx->begin() FAILED");
        while (true) delay(1000);
    }
    gfx->fillScreen(BLACK);

    // Hook VSYNC so each refresh starts on a frame boundary. attachInterrupt only
    // enables the input buffer; pinMode(INPUT) here would disable the LCD's output.
    attachInterrupt(digitalPinToInterrupt(TFT_VSYNC), on_rgb_vsync, FALLING);
    vsync_sync = vblank();
    Serial.printf("VSYNC sync: %s\n", vsync_sync ? "on" : "off");

    if (!psramFound()) {
        halt_with_screen("PSRAM not detected - wrong board variant?");
    }

    Wire.begin(TOUCH_SDA, TOUCH_SCL);
    Wire.setClock(100000);

#if FORCE_TOUCH_RESET
    gt911_reset_via_expander();
#endif

    if (gt911_probe(0x5D)) {
        gt911_addr = 0x5D;
    } else if (gt911_probe(0x14)) {
        gt911_addr = 0x14;
    } else {
        Serial.println("GT911 silent, pulsing reset via PCA9557");
        gt911_reset_via_expander();
        if (gt911_probe(0x5D)) gt911_addr = 0x5D;
        else if (gt911_probe(0x14)) gt911_addr = 0x14;
        else Serial.println("GT911 still silent");
    }
    Serial.printf("GT911 @ 0x%02X\n", gt911_addr);

    lv_init();
    // Render in internal SRAM: it keeps the PSRAM bus free for the RGB panel's
    // scanout, which otherwise starves and blanks the screen mid-tap.
    lvgl_buf = (lv_color_t *)heap_caps_malloc(SCREEN_W * DRAW_BUF_LINES * sizeof(lv_color_t),
                                              MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!lvgl_buf) {
        lvgl_buf = (lv_color_t *)heap_caps_malloc(SCREEN_W * DRAW_BUF_LINES * sizeof(lv_color_t),
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!lvgl_buf) halt_with_screen("out of memory for LVGL buffer");
    Serial.printf("LVGL draw buffer: %u B, free internal %u B\n",
                  (unsigned)(SCREEN_W * DRAW_BUF_LINES * sizeof(lv_color_t)),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    lv_disp_draw_buf_init(&draw_buf, lvgl_buf, nullptr, SCREEN_W * DRAW_BUF_LINES);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SCREEN_W;
    disp_drv.ver_res = SCREEN_H;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_touch_cb;
    lv_indev_drv_register(&indev_drv);

    lvgl_mux = xSemaphoreCreateMutex();
    if (!lvgl_mux) halt_with_screen("failed to create LVGL mutex");
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8 * 1024, nullptr, 1, nullptr, 1);

    tap_queue = xQueueCreate(TAP_QUEUE_LEN, sizeof(TapRequest));
    if (!tap_queue) halt_with_screen("failed to create tap queue");
    xTaskCreatePinnedToCore(hid_task, "hid", 8 * 1024, nullptr, 1, nullptr, 0);

    // Build the UI immediately so the user sees something while BLE boots.
    xSemaphoreTake(lvgl_mux, portMAX_DELAY);
    build_ui();
    set_status("starting Bluetooth...", C_AMBER);
    set_pair_status("awaiting pairing", C_AMBER);
    xSemaphoreGive(lvgl_mux);

    // Initialize Bluedroid under the advertised device name. The deck keeps
    // its factory public address for life: bonding requires the advertised
    // address and the SMP identity address to match, and Bluedroid always
    // distributes the public one. Bonds are stored in NVS by the stack and
    // survive reboots, so Windows reconnects with the stored keys.
    BLEDevice::init("BT Stream Deck");
    BLEDevice::setCustomGapHandler(gap_log_handler);
    Serial.printf("BLE: bond count at boot = %d\n", esp_ble_get_bond_device_num());

    // The BLE library calls BLEDevice::init() again; it is guarded internally,
    // then creates the HID service and starts advertising.
    bleKeyboard.begin();

    // Start connection monitoring after the BLE stack has had a moment.
    xTaskCreatePinnedToCore(connection_task, "ble_status", 3 * 1024, nullptr, 1, nullptr, 0);

    Serial.println("setup done");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(200));
}
