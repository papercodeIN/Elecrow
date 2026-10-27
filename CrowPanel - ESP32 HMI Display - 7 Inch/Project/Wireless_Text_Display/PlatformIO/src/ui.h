#ifndef _SQUARELINE_PROJECT_UI_H
#define _SQUARELINE_PROJECT_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined __has_include
#if __has_include("lvgl.h")
#include "lvgl.h"
#elif __has_include("lvgl/lvgl.h")
#include "lvgl/lvgl.h"
#else
#include "lvgl.h"
#endif
#else
#include "lvgl.h"
#endif

#include "ui_helpers.h"
#include "ui_events.h"

// SCREEN: ui_Screen1
void ui_Screen1_screen_init(void);
extern lv_obj_t * ui_Screen1;
extern lv_obj_t * ui_TextLabel[5]; // Array of 5 text labels
extern lv_obj_t * ui_StatusLabel;

// SCREEN: ui_APScreen (shown when WiFi is unconfigured/unreachable)
void ui_APScreen_screen_init(void);
extern lv_obj_t * ui_APScreen;
extern lv_obj_t * ui_APTitleLabel;
extern lv_obj_t * ui_APSSIDLabel;
extern lv_obj_t * ui_APPassLabel;
extern lv_obj_t * ui_APIPLabel;
// CUSTOM VARIABLES

// EVENTS

extern lv_obj_t * ui____initial_actions0;

// UI INIT
void ui_init(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
