// AP configuration screen: shown when WiFi is not configured or unreachable.
// Matches the web UI palette; the content block is centered vertically so the
// screen does not look bottom-heavy.

#include <stdio.h>
#include "ui.h"

#define COL_BG      lv_color_hex(0x0D1117)
#define COL_PANEL   lv_color_hex(0x161B22)
#define COL_BORDER  lv_color_hex(0x2B3440)
#define COL_CYAN    lv_color_hex(0x00E0FF)
#define COL_DIM     lv_color_hex(0x8B98A5)
#define COL_TEXT    lv_color_hex(0xE6EDF3)
#define COL_GREEN   lv_color_hex(0x3FB950)
#define COL_RED     lv_color_hex(0xF85149)

// One info card: dim caption on the left, large value on the right
static void apinfo_card(lv_obj_t * parent, const char * caption, const char * value,
                        lv_coord_t y, lv_color_t value_color)
{
    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_set_pos(card, 110, y);
    lv_obj_set_size(card, 580, 60);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(card, COL_PANEL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(card, COL_BORDER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(card, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(card, true, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * accent = lv_obj_create(card);
    lv_obj_set_pos(accent, 0, 0);
    lv_obj_set_size(accent, 4, 60);
    lv_obj_clear_flag(accent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(accent, COL_CYAN, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(accent, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(accent, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * cap = lv_label_create(card);
    lv_obj_set_pos(cap, 20, 0);
    lv_obj_set_align(cap, LV_ALIGN_LEFT_MID);
    lv_label_set_text(cap, caption);
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(cap, COL_DIM, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(cap, 2, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * val = lv_label_create(card);
    lv_obj_set_pos(val, -20, 0);
    lv_obj_set_align(val, LV_ALIGN_RIGHT_MID);
    lv_label_set_text(val, value);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(val, value_color, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void ui_APScreen_screen_init(void)
{
    ui_APScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_APScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_APScreen, COL_BG, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_APScreen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ui_APScreen, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Title + subtitle centered near the top of the content block
    ui_APTitleLabel = lv_label_create(ui_APScreen);
    lv_obj_set_pos(ui_APTitleLabel, 0, 48);
    lv_obj_set_align(ui_APTitleLabel, LV_ALIGN_TOP_MID);
    lv_label_set_text(ui_APTitleLabel, "WIFI NOT CONFIGURED");
    lv_obj_set_style_text_font(ui_APTitleLabel, &lv_font_montserrat_36, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_APTitleLabel, COL_RED, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(ui_APTitleLabel, 3, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * subtitle = lv_label_create(ui_APScreen);
    lv_obj_set_pos(subtitle, 0, 100);
    lv_obj_set_align(subtitle, LV_ALIGN_TOP_MID);
    lv_label_set_text(subtitle, "SET UP FROM ANY PHONE OR PC");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(subtitle, COL_DIM, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(subtitle, 2, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Three info cards, evenly spaced through the middle of the screen
    apinfo_card(ui_APScreen, "NETWORK (SSID)", "CrowPanel_Display", 160, COL_CYAN);
    apinfo_card(ui_APScreen, "PASSWORD", "12345678", 234, COL_CYAN);
    apinfo_card(ui_APScreen, "WEB PAGE", "http://192.168.4.1", 308, COL_GREEN);

    // Step hint across the bottom
    lv_obj_t * hint = lv_label_create(ui_APScreen);
    lv_obj_set_pos(hint, 0, 412);
    lv_obj_set_align(hint, LV_ALIGN_TOP_MID);
    lv_label_set_text(hint, "1. JOIN NETWORK      2. OPEN WEB PAGE      3. SAVE YOUR WIFI");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(hint, COL_DIM, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(hint, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
}
