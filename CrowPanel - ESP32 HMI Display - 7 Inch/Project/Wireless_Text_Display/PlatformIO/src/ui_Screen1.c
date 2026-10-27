// Tech-HUD themed UI for Wireless Text Display
// Matches the web UI palette: #0d1117 bg, #161b22 panels, #2b3440 borders,
// cyan #00e0ff headings, dim #8b98a5 secondary text.
// Designed for CrowPanel 7" ESP32-S3 HMI (800x480)

#include <stdio.h>
#include "ui.h"

#define COL_BG      lv_color_hex(0x0D1117)
#define COL_PANEL   lv_color_hex(0x161B22)
#define COL_BORDER  lv_color_hex(0x2B3440)
#define COL_CYAN    lv_color_hex(0x00E0FF)
#define COL_DIM     lv_color_hex(0x8B98A5)
#define COL_TEXT    lv_color_hex(0xE6EDF3)
#define COL_GREEN   lv_color_hex(0x3FB950)

void ui_Screen1_screen_init(void)
{
    ui_Screen1 = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_Screen1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Screen1, COL_BG, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Screen1, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ui_Screen1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* ---------- Header ---------- */
    lv_obj_t * header = lv_obj_create(ui_Screen1);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, 800, 52);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(header, COL_PANEL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(header, COL_BORDER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(header, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(header, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(header, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * title = lv_label_create(header);
    lv_obj_set_pos(title, 16, 0);
    lv_obj_set_align(title, LV_ALIGN_LEFT_MID);
    lv_label_set_text(title, "CROWPANEL TEXT DISPLAY");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(title, COL_CYAN, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(title, 3, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * sysState = lv_label_create(header);
    lv_obj_set_pos(sysState, -16, 0);
    lv_obj_set_align(sysState, LV_ALIGN_RIGHT_MID);
    lv_label_set_text(sysState, "SYS.ONLINE");
    lv_obj_set_style_text_font(sysState, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(sysState, COL_GREEN, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(sysState, 2, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* ---------- 5 line cards ---------- */
    for(int i = 0; i < 5; i++) {
        lv_obj_t * card = lv_obj_create(ui_Screen1);
        lv_obj_set_pos(card, 12, 62 + i * 74);
        lv_obj_set_size(card, 776, 70);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(card, COL_PANEL, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(card, COL_BORDER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(card, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(card, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_clip_corner(card, true, LV_PART_MAIN | LV_STATE_DEFAULT);

        // Cyan accent edge on the left, echoing the web page's accent bars
        lv_obj_t * accent = lv_obj_create(card);
        lv_obj_set_pos(accent, 0, 0);
        lv_obj_set_size(accent, 4, 70);
        lv_obj_clear_flag(accent, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(accent, COL_CYAN, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(accent, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(accent, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

        // Channel number, dim like the web page's row captions
        lv_obj_t * chan = lv_label_create(card);
        lv_obj_set_pos(chan, 20, 0);
        lv_obj_set_align(chan, LV_ALIGN_LEFT_MID);
        char chanText[8];
        sprintf(chanText, "%02d", i + 1);
        lv_label_set_text(chan, chanText);
        lv_obj_set_style_text_font(chan, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(chan, COL_DIM, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_letter_space(chan, 1, LV_PART_MAIN | LV_STATE_DEFAULT);

        // Payload text
        ui_TextLabel[i] = lv_label_create(card);
        lv_obj_set_pos(ui_TextLabel[i], 64, 0);
        lv_obj_set_width(ui_TextLabel[i], 690);
        lv_obj_set_height(ui_TextLabel[i], LV_SIZE_CONTENT);
        lv_obj_set_align(ui_TextLabel[i], LV_ALIGN_LEFT_MID);

        char defaultText[20];
        sprintf(defaultText, "LINE %d", i + 1);
        lv_label_set_text(ui_TextLabel[i], defaultText);

        lv_obj_set_style_text_font(ui_TextLabel[i], &lv_font_montserrat_48, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(ui_TextLabel[i], COL_TEXT, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(ui_TextLabel[i], LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(ui_TextLabel[i], LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_long_mode(ui_TextLabel[i], LV_LABEL_LONG_DOT);
    }

    /* ---------- Status bar ---------- */
    lv_obj_t * status = lv_obj_create(ui_Screen1);
    lv_obj_set_pos(status, 0, 444);
    lv_obj_set_size(status, 800, 36);
    lv_obj_clear_flag(status, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(status, COL_PANEL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(status, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(status, COL_BORDER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(status, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(status, LV_BORDER_SIDE_TOP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(status, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(status, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * prefix = lv_label_create(status);
    lv_obj_set_pos(prefix, 16, 0);
    lv_obj_set_align(prefix, LV_ALIGN_LEFT_MID);
    lv_label_set_text(prefix, "WIFI IP:");
    lv_obj_set_style_text_font(prefix, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(prefix, COL_DIM, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(prefix, 2, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_StatusLabel = lv_label_create(status);
    lv_obj_set_width(ui_StatusLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_StatusLabel, LV_SIZE_CONTENT);
    lv_obj_set_pos(ui_StatusLabel, 100, 0);
    lv_obj_set_align(ui_StatusLabel, LV_ALIGN_LEFT_MID);
    lv_label_set_text(ui_StatusLabel, "CONNECTING...");
    lv_obj_set_style_text_font(ui_StatusLabel, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_StatusLabel, COL_GREEN, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(ui_StatusLabel, 1, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * hwTag = lv_label_create(status);
    lv_obj_set_pos(hwTag, -16, 0);
    lv_obj_set_align(hwTag, LV_ALIGN_RIGHT_MID);
    lv_label_set_text(hwTag, "ESP32-S3 // 800x480");
    lv_obj_set_style_text_font(hwTag, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(hwTag, COL_DIM, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(hwTag, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
}
