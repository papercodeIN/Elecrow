// Tech-HUD themed LVGL UI for Wireless Text Display (CrowPanel ESP32-S3 7" HMI)

#include "ui.h"
#include "ui_helpers.h"

///////////////////// VARIABLES ////////////////////

// SCREEN: ui_Screen1
void ui_Screen1_screen_init(void);
lv_obj_t * ui_Screen1;
lv_obj_t * ui_TextLabel[5]; // Array of 5 text labels
lv_obj_t * ui_StatusLabel;

// SCREEN: ui_APScreen
lv_obj_t * ui_APScreen;
lv_obj_t * ui_APTitleLabel;
lv_obj_t * ui_APSSIDLabel;
lv_obj_t * ui_APPassLabel;
lv_obj_t * ui_APIPLabel;
lv_obj_t * ui_HeaderBar;
lv_obj_t * ui_HeaderLabel;
lv_obj_t * ui_HeaderStatus;
lv_obj_t * ui_StatusBar;
lv_obj_t * ui_StatusPrefix;
// CUSTOM VARIABLES

// EVENTS
lv_obj_t * ui____initial_actions0;

// IMAGES AND IMAGE SETS

///////////////////// TEST LVGL SETTINGS /////////////////////
#if LV_COLOR_DEPTH != 16
    #error "LV_COLOR_DEPTH should be 16bit to match SquareLine Studio's settings"
#endif
#if LV_COLOR_16_SWAP != 0
    #error "LV_COLOR_16_SWAP should be 0 to match SquareLine Studio's settings"
#endif

///////////////////// ANIMATIONS /////////////////////

///////////////////// FUNCTIONS /////////////////////

///////////////////// SCREENS /////////////////////

void ui_init(void)
{
    lv_disp_t * dispp = lv_disp_get_default();
    // Custom tech theme: cyan accent, dark background
    lv_theme_t * theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED),
                                               true, LV_FONT_DEFAULT);
    lv_disp_set_theme(dispp, theme);
    ui_Screen1_screen_init();
    ui____initial_actions0 = lv_obj_create(NULL);
    lv_disp_load_scr(ui_Screen1);
}
