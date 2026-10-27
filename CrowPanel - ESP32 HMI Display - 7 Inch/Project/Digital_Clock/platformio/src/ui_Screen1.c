// CHRONO dashboard for the CrowPanel 7" (800x480), ported from the 2.8" reference design.
// LVGL version: 8.3.11

#include "ui.h"

// Palette (RGB888 ports of the 2.8" TFT_eSPI project)
#define CHRONO_BG        0x020B1E  // deep navy background (as 2.8" reference)
#define CHRONO_HEADER_BG 0x0A2A6A  // header / footer / panel fill
#define CHRONO_PANEL_BG  0x0A2A6A  // instrument panel fill (same blue as header)
#define CHRONO_GRID      0x1A4C9E  // grid / divider lines
#define CHRONO_DIM       0x63A4FF  // dim blue labels
#define CHRONO_AMBER     0xFFC400  // AM/PM, percentage
#define CHRONO_GREEN     0x00E676  // OK state
#define CHRONO_ACCENT    0x00E5FF  // cyan accent (digits, borders)

// Seven-segment geometry: digit cell 64x110, segment thickness 10
#define DIG_W   64
#define DIG_H   110
#define SEG_T   10

static const lv_coord_t segGeo[7][4] = {
    //  x        y        w        h
    { 2,       0,       DIG_W - 4, SEG_T },      // A top
    { DIG_W - SEG_T, 2, SEG_T, (DIG_H / 2) - 4 }, // B top-right
    { DIG_W - SEG_T, (DIG_H / 2) + 2, SEG_T, (DIG_H / 2) - 4 }, // C bottom-right
    { 2,       DIG_H - SEG_T, DIG_W - 4, SEG_T }, // D bottom
    { 0,       (DIG_H / 2) + 2, SEG_T, (DIG_H / 2) - 4 }, // E bottom-left
    { 0,       2,       SEG_T, (DIG_H / 2) - 4 }, // F top-left
    { 2,       (DIG_H / 2) - (SEG_T / 2), DIG_W - 4, SEG_T }, // G middle
};

// Thin solid rectangle used for lines and borders
static lv_obj_t * chrono_rect(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                              lv_coord_t w, lv_coord_t h, uint32_t color)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_pos(r, x, y);
    lv_obj_set_size(r, w, h);
    lv_obj_set_style_bg_color(r, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    return r;
}

static lv_obj_t * chrono_label(lv_obj_t *parent, const char *text, lv_coord_t cx, lv_coord_t cy,
                               const lv_font_t *font, uint32_t color, uint8_t letterSpace)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_align(l, LV_ALIGN_CENTER, cx, cy);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(l, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(l, letterSpace, LV_PART_MAIN | LV_STATE_DEFAULT);
    return l;
}

// Seven-segment digit: transparent 64x110 cell holding 7 toggleable segments
static void chrono_digit(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_obj_t *out[7])
{
    lv_obj_t *cell = lv_obj_create(parent);
    lv_obj_remove_style_all(cell);
    lv_obj_set_pos(cell, x, y);
    lv_obj_set_size(cell, DIG_W, DIG_H);
    for (int i = 0; i < 7; i++) {
        out[i] = chrono_rect(cell, segGeo[i][0], segGeo[i][1], segGeo[i][2], segGeo[i][3], CHRONO_ACCENT);
        lv_obj_set_style_bg_opa(out[i], LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

// Colon: two stacked squares in an 8x110 cell
static void chrono_colon(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_obj_t *out[2])
{
    lv_obj_t *cell = lv_obj_create(parent);
    lv_obj_remove_style_all(cell);
    lv_obj_set_pos(cell, x, y);
    lv_obj_set_size(cell, 8, DIG_H);
    out[0] = chrono_rect(cell, 0, DIG_H / 4 - 4, 10, 10, CHRONO_ACCENT);
    out[1] = chrono_rect(cell, 0, 3 * DIG_H / 4 - 4, 10, 10, CHRONO_ACCENT);
}

void ui_Screen1_screen_init(void)
{
    ui_Screen1 = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_Screen1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Screen1, lv_color_hex(CHRONO_BG), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Screen1, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    // ---- Header bar ----
    lv_obj_t *header = chrono_rect(ui_Screen1, 0, 0, 800, 46, CHRONO_HEADER_BG);
    (void)header;
    ui_HeaderAccent = chrono_rect(ui_Screen1, 0, 46, 800, 2, CHRONO_ACCENT);

    chrono_label(ui_Screen1, "CHRONO", -316, -217, &lv_font_montserrat_24, CHRONO_ACCENT, 6);

    ui_WifiDot = lv_obj_create(ui_Screen1);
    lv_obj_remove_style_all(ui_WifiDot);
    lv_obj_set_size(ui_WifiDot, 12, 12);
    lv_obj_align(ui_WifiDot, LV_ALIGN_TOP_LEFT, 262, 17);
    lv_obj_set_style_radius(ui_WifiDot, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_WifiDot, lv_color_hex(CHRONO_AMBER), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_WifiDot, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_WifiLabel = chrono_label(ui_Screen1, "NO WIFI", 24, -11, &lv_font_montserrat_16, CHRONO_AMBER, 2);
    lv_obj_align(ui_WifiLabel, LV_ALIGN_TOP_LEFT, 282, 15);

    ui_NtpDot = lv_obj_create(ui_Screen1);
    lv_obj_remove_style_all(ui_NtpDot);
    lv_obj_set_size(ui_NtpDot, 12, 12);
    lv_obj_align(ui_NtpDot, LV_ALIGN_TOP_LEFT, 448, 17);
    lv_obj_set_style_radius(ui_NtpDot, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_NtpDot, lv_color_hex(CHRONO_AMBER), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_NtpDot, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_NtpLabel = chrono_label(ui_Screen1, "NO SYNC", 24, -11, &lv_font_montserrat_16, CHRONO_AMBER, 2);
    lv_obj_align(ui_NtpLabel, LV_ALIGN_TOP_LEFT, 468, 15);

    ui_FormatIndicator = chrono_label(ui_Screen1, "12H", 0, 0, &lv_font_montserrat_20, CHRONO_AMBER, 4);
    lv_obj_align(ui_FormatIndicator, LV_ALIGN_TOP_RIGHT, -16, 12);

    // ---- Main instrument panel ----
    ui_PanelFrame = lv_obj_create(ui_Screen1);
    lv_obj_set_pos(ui_PanelFrame, 20, 56);
    lv_obj_set_size(ui_PanelFrame, 760, 340);
    lv_obj_clear_flag(ui_PanelFrame, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(ui_PanelFrame, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_PanelFrame, lv_color_hex(CHRONO_PANEL_BG), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_PanelFrame, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui_PanelFrame, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_PanelFrame, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_PanelFrame, lv_color_hex(CHRONO_ACCENT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_PanelFrame, LV_OPA_80, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Internal grid: vertical divider between the two time columns
    chrono_rect(ui_PanelFrame, 379, 48, 2, 110, CHRONO_GRID);
    chrono_rect(ui_PanelFrame, 14, 36, 732, 1, CHRONO_GRID);

    // Timezone labels
    ui_IstColLabel = chrono_label(ui_PanelFrame, "IST", -190, -150, &lv_font_montserrat_18, CHRONO_ACCENT, 6);
    ui_UkColLabel  = chrono_label(ui_PanelFrame, "UK",  190, -150, &lv_font_montserrat_18, CHRONO_ACCENT, 6);

    // Seven-segment digits: IST column centered at x=190 (panel coords), UK at x=570
    lv_coord_t istX[4] = {50, 118, 198, 266};
    lv_coord_t ukX[4]  = {430, 498, 578, 646};
    for (int i = 0; i < 4; i++) {
        chrono_digit(ui_PanelFrame, istX[i], 48, ui_IstDigits[i]);
        chrono_digit(ui_PanelFrame, ukX[i], 48, ui_UkDigits[i]);
    }
    chrono_colon(ui_PanelFrame, 186, 48, ui_IstColon);
    chrono_colon(ui_PanelFrame, 566, 48, ui_UkColon);

    // AM/PM markers under each column
    ui_AmPmIst = chrono_label(ui_PanelFrame, "", -190, 4, &lv_font_montserrat_16, CHRONO_AMBER, 4);
    ui_AmPmUk  = chrono_label(ui_PanelFrame, "",  190, 4, &lv_font_montserrat_16, CHRONO_AMBER, 4);

    // ---- Date inside the panel (as in the 2.8" reference) ----
    ui_DayLabel = chrono_label(ui_PanelFrame, "---", 0, 54, &lv_font_montserrat_20, CHRONO_DIM, 6);
    ui_DateLabel = chrono_label(ui_PanelFrame, "NO TIME", 0, 88, &lv_font_montserrat_28, 0xFFFFFF, 2);

    // ---- Day progress bar inside the panel ----
    lv_obj_t *barBg = lv_obj_create(ui_PanelFrame);
    lv_obj_remove_style_all(barBg);
    lv_obj_set_pos(barBg, 80, 286);
    lv_obj_set_size(barBg, 600, 12);
    lv_obj_set_style_bg_color(barBg, lv_color_hex(0x0E1E4A), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(barBg, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(barBg, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(barBg, lv_color_hex(CHRONO_GRID), LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_DayBarFill = chrono_rect(barBg, 0, 0, 0, 12, CHRONO_ACCENT);

    ui_PctLabel = chrono_label(ui_PanelFrame, "0%", 0, 142, &lv_font_montserrat_14, CHRONO_AMBER, 2);

    // ---- Status line in the gap above the footer (also used by "time details" touch) ----
    ui_StatusLabel = chrono_label(ui_Screen1, "TAP PANEL FOR TIME DETAILS", 0, 178, &lv_font_montserrat_14, CHRONO_DIM, 2);

    // ---- Footer ----
    chrono_rect(ui_Screen1, 0, 440, 800, 40, CHRONO_HEADER_BG);
    ui_FooterAccent = chrono_rect(ui_Screen1, 0, 440, 800, 2, CHRONO_ACCENT);

    // Footer labels anchored inside the footer bar (y=452): NETWORK, then IP beside it
    lv_obj_t *netLabel = chrono_label(ui_Screen1, "NETWORK", 0, 0, &lv_font_montserrat_16, CHRONO_DIM, 2);
    lv_obj_align(netLabel, LV_ALIGN_TOP_LEFT, 140, 452);
    ui_IpLabel = chrono_label(ui_Screen1, "NO WIFI", 0, 0, &lv_font_montserrat_16, CHRONO_AMBER, 2);
    lv_obj_align(ui_IpLabel, LV_ALIGN_TOP_LEFT, 250, 452);

    // ---- Control buttons (inside the footer bar) ----
    ui_BrightnessDownBtn = lv_btn_create(ui_Screen1);
    lv_obj_set_pos(ui_BrightnessDownBtn, 8, 443);
    lv_obj_set_size(ui_BrightnessDownBtn, 56, 34);
    lv_obj_set_style_bg_color(ui_BrightnessDownBtn, lv_color_hex(0x0E1E4A), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui_BrightnessDownBtn, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_BrightnessDownBtn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_BrightnessDownBtn, lv_color_hex(CHRONO_GRID), LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_BrightnessDownLabel = lv_label_create(ui_BrightnessDownBtn);
    lv_obj_center(ui_BrightnessDownLabel);
    lv_label_set_text(ui_BrightnessDownLabel, "-");
    lv_obj_set_style_text_font(ui_BrightnessDownLabel, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_BrightnessDownLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_BrightnessUpBtn = lv_btn_create(ui_Screen1);
    lv_obj_set_pos(ui_BrightnessUpBtn, 72, 443);
    lv_obj_set_size(ui_BrightnessUpBtn, 56, 34);
    lv_obj_set_style_bg_color(ui_BrightnessUpBtn, lv_color_hex(0x0E1E4A), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui_BrightnessUpBtn, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_BrightnessUpBtn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_BrightnessUpBtn, lv_color_hex(CHRONO_GRID), LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_BrightnessUpLabel = lv_label_create(ui_BrightnessUpBtn);
    lv_obj_center(ui_BrightnessUpLabel);
    lv_label_set_text(ui_BrightnessUpLabel, "+");
    lv_obj_set_style_text_font(ui_BrightnessUpLabel, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_BrightnessUpLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_ThemeBtn = lv_btn_create(ui_Screen1);
    lv_obj_set_pos(ui_ThemeBtn, 656, 443);
    lv_obj_set_size(ui_ThemeBtn, 56, 34);
    lv_obj_set_style_bg_color(ui_ThemeBtn, lv_color_hex(0x0E1E4A), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui_ThemeBtn, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_ThemeBtn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_ThemeBtn, lv_color_hex(CHRONO_GRID), LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_ThemeLabel = lv_label_create(ui_ThemeBtn);
    lv_obj_center(ui_ThemeLabel);
    lv_label_set_text(ui_ThemeLabel, "T");
    lv_obj_set_style_text_font(ui_ThemeLabel, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_ThemeLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_FormatBtn = lv_btn_create(ui_Screen1);
    lv_obj_set_pos(ui_FormatBtn, 720, 443);
    lv_obj_set_size(ui_FormatBtn, 72, 34);
    lv_obj_set_style_bg_color(ui_FormatBtn, lv_color_hex(0x0E1E4A), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui_FormatBtn, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_FormatBtn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_FormatBtn, lv_color_hex(CHRONO_GRID), LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_FormatLabel = lv_label_create(ui_FormatBtn);
    lv_obj_center(ui_FormatLabel);
    lv_label_set_text(ui_FormatLabel, "12H");
    lv_obj_set_style_text_font(ui_FormatLabel, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_FormatLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);

    // ---- Invisible touch overlay over the instrument panel (shows time details) ----
    ui_TimeArea = lv_obj_create(ui_Screen1);
    lv_obj_set_pos(ui_TimeArea, 20, 56);
    lv_obj_set_size(ui_TimeArea, 760, 340);
    lv_obj_set_style_bg_opa(ui_TimeArea, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_TimeArea, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);

    // ---- Event callbacks ----
    lv_obj_add_event_cb(ui_BrightnessUpBtn, ui_event_BrightnessUp, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_BrightnessDownBtn, ui_event_BrightnessDown, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_ThemeBtn, ui_event_ThemeChange, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_FormatBtn, ui_event_FormatToggle, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_TimeArea, ui_event_TimeTouch, LV_EVENT_CLICKED, NULL);
}
