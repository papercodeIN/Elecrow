// CHRONO dashboard logic for the CrowPanel 7" (800x480).
// LVGL version: 8.3.11

#include "ui.h"
#include "ui_helpers.h"
#include <stdio.h>

///////////////////// VARIABLES ////////////////////

// SCREEN: ui_Screen1
void ui_Screen1_screen_init(void);
lv_obj_t * ui_Screen1;
lv_obj_t * ui_DayLabel;
lv_obj_t * ui_DateLabel;
lv_obj_t * ui_StatusLabel;
lv_obj_t * ui_BrightnessUpBtn;
lv_obj_t * ui_BrightnessUpLabel;
lv_obj_t * ui_BrightnessDownBtn;
lv_obj_t * ui_BrightnessDownLabel;
lv_obj_t * ui_ThemeBtn;
lv_obj_t * ui_ThemeLabel;
lv_obj_t * ui_FormatBtn;
lv_obj_t * ui_FormatLabel;
lv_obj_t * ui_TimeArea;

// CHRONO dashboard widgets
lv_obj_t * ui_WifiDot;
lv_obj_t * ui_WifiLabel;
lv_obj_t * ui_NtpDot;
lv_obj_t * ui_NtpLabel;
lv_obj_t * ui_FormatIndicator;
lv_obj_t * ui_IpLabel;
lv_obj_t * ui_DayBarFill;
lv_obj_t * ui_PctLabel;
lv_obj_t * ui_AmPmIst;
lv_obj_t * ui_AmPmUk;
lv_obj_t * ui_PanelFrame;
lv_obj_t * ui_IstColLabel;
lv_obj_t * ui_UkColLabel;
lv_obj_t * ui_HeaderAccent;
lv_obj_t * ui_FooterAccent;
lv_obj_t * ui_IstDigits[4][7];
lv_obj_t * ui_UkDigits[4][7];
lv_obj_t * ui_IstColon[2];
lv_obj_t * ui_UkColon[2];

// CUSTOM VARIABLES

// EVENTS
lv_obj_t * ui____initial_actions0;

// IMAGES AND IMAGE SETS

///////////////////// TEST LVGL SETTINGS ////////////////////
#if LV_COLOR_DEPTH != 16
    #error "LV_COLOR_DEPTH should be 16bit to match SquareLine Studio's settings"
#endif
#if LV_COLOR_16_SWAP !=0
    #error "LV_COLOR_16_SWAP should be 0 to match SquareLine Studio's settings"
#endif

///////////////////// ANIMATIONS ////////////////////

///////////////////// FUNCTIONS ////////////////////

// Seven-segment lookup table (index 10 = blank)
static const uint8_t SEGMAP[11] = {
  0x3F, 0x06, 0x5B, 0x4F, 0x66,
  0x6D, 0x7D, 0x07, 0x7F, 0x6F,
  0x00
};

static void set_digit(lv_obj_t *digit[7], int value)
{
    uint8_t seg = SEGMAP[value];
    for (int i = 0; i < 7; i++) {
        lv_obj_set_style_bg_opa(digit[i], (seg >> i) & 1 ? LV_OPA_COVER : LV_OPA_TRANSP,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

void ui_update_time_columns(int istHour, int istMin, const char *istAmPm,
                            int ukHour, int ukMin, const char *ukAmPm)
{
    static int lastIstH = -1, lastIstM = -1, lastUkH = -1, lastUkM = -1;
    if (istHour != lastIstH || istMin != lastIstM) {
        set_digit(ui_IstDigits[0], (istHour / 10) % 10);
        set_digit(ui_IstDigits[1], istHour % 10);
        set_digit(ui_IstDigits[2], (istMin / 10) % 10);
        set_digit(ui_IstDigits[3], istMin % 10);
        lv_label_set_text(ui_AmPmIst, istAmPm ? istAmPm : "");
        lastIstH = istHour;
        lastIstM = istMin;
    }
    if (ukHour != lastUkH || ukMin != lastUkM) {
        set_digit(ui_UkDigits[0], (ukHour / 10) % 10);
        set_digit(ui_UkDigits[1], ukHour % 10);
        set_digit(ui_UkDigits[2], (ukMin / 10) % 10);
        set_digit(ui_UkDigits[3], ukMin % 10);
        lv_label_set_text(ui_AmPmUk, ukAmPm ? ukAmPm : "");
        lastUkH = ukHour;
        lastUkM = ukMin;
    }
}

void ui_blank_time_columns(void)
{
    for (int i = 0; i < 4; i++) {
        set_digit(ui_IstDigits[i], 10);
        set_digit(ui_UkDigits[i], 10);
    }
    lv_label_set_text(ui_AmPmIst, "");
    lv_label_set_text(ui_AmPmUk, "");
}

void ui_set_colons_visible(bool visible)
{
    lv_obj_t *squares[4] = {ui_IstColon[0], ui_IstColon[1], ui_UkColon[0], ui_UkColon[1]};
    for (int i = 0; i < 4; i++) {
        if (visible) lv_obj_clear_flag(squares[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(squares[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_set_ampm_visible(bool visible)
{
    lv_obj_t *cols[2] = {ui_AmPmIst, ui_AmPmUk};
    for (int i = 0; i < 2; i++) {
        if (visible) lv_obj_clear_flag(cols[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(cols[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_update_date(const char *day, const char *date)
{
    lv_label_set_text(ui_DayLabel, day);
    lv_label_set_text(ui_DateLabel, date);
}

void ui_update_day_progress(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    lv_obj_set_width(ui_DayBarFill, (lv_coord_t)(598 * pct / 100));
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    lv_label_set_text(ui_PctLabel, buf);
}

void ui_update_network(bool wifiUp, bool ntpOk, const char *state, const char *ip)
{
    uint32_t okColor = 0x00E676;
    uint32_t warnColor = 0xFFC400;
    uint32_t wifiColor = wifiUp ? okColor : warnColor;

    lv_obj_set_style_bg_color(ui_WifiDot, lv_color_hex(wifiColor), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_WifiLabel, lv_color_hex(wifiColor), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(ui_WifiLabel, wifiUp ? "WIFI" : "NO WIFI");

    uint32_t ntpColor = ntpOk ? okColor : warnColor;
    lv_obj_set_style_bg_color(ui_NtpDot, lv_color_hex(ntpColor), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_NtpLabel, lv_color_hex(ntpColor), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(ui_NtpLabel, state);

    lv_obj_set_style_text_color(ui_IpLabel, lv_color_hex(wifiColor), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(ui_IpLabel, ip);
}

// Restyle the clock: digits / colons / bar fill take the accent color,
// while the chrome (borders, column labels, header/footer rules) stays cyan.
void ui_apply_accent(uint32_t color)
{
    lv_color_t c = lv_color_hex(color);
    lv_color_t chrome = lv_color_hex(0x00E5FF);
    for (int i = 0; i < 4; i++) {
        for (int s = 0; s < 7; s++) {
            lv_obj_set_style_bg_color(ui_IstDigits[i][s], c, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(ui_UkDigits[i][s], c, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
    lv_obj_t *colons[4] = {ui_IstColon[0], ui_IstColon[1], ui_UkColon[0], ui_UkColon[1]};
    for (int i = 0; i < 4; i++) {
        lv_obj_set_style_bg_color(colons[i], c, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    lv_obj_set_style_bg_color(ui_DayBarFill, c, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_IstColLabel, chrome, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_UkColLabel, chrome, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_PanelFrame, chrome, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_HeaderAccent, chrome, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_FooterAccent, chrome, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void ui_event_BrightnessUp(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if(event_code == LV_EVENT_CLICKED) {
        increaseBrightness();
    }
}

void ui_event_BrightnessDown(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if(event_code == LV_EVENT_CLICKED) {
        decreaseBrightness();
    }
}

void ui_event_ThemeChange(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);

    if(event_code == LV_EVENT_PRESSED) {
        lv_obj_set_style_bg_color(target, lv_color_hex(0x1A3A7A), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    else if(event_code == LV_EVENT_RELEASED || event_code == LV_EVENT_CLICKED) {
        lv_obj_set_style_bg_color(target, lv_color_hex(0x0E1E4A), LV_PART_MAIN | LV_STATE_DEFAULT);
        if(event_code == LV_EVENT_CLICKED) {
            changeColorTheme();
        }
    }
}

void ui_event_FormatToggle(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if(event_code == LV_EVENT_CLICKED) {
        toggleTimeFormat();
    }
}

void ui_event_TimeTouch(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if(event_code == LV_EVENT_CLICKED) {
        showTimeDetails();
    }
}

///////////////////// SCREENS ////////////////////

void ui_init(void)
{
    lv_disp_t * dispp = lv_disp_get_default();
    lv_theme_t * theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED),
                                               true, LV_FONT_DEFAULT);
    lv_disp_set_theme(dispp, theme);
    ui_Screen1_screen_init();
    ui____initial_actions0 = lv_obj_create(NULL);
    lv_disp_load_scr(ui_Screen1);
}
