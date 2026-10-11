// display_utils.h - Display utility functions for ESP32 Weather Clock
#pragma once
#include "EPD.h"

// Add display-related helper functions here as needed
// Example: clear and redraw the full screen
inline void refreshDisplay(uint8_t* imageBuffer) {
    EPD_Clear();
    Paint_NewImage(imageBuffer, EPD_W, EPD_H, 0, WHITE);
    Paint_Clear(WHITE);
    EPD_Display(imageBuffer);
}
