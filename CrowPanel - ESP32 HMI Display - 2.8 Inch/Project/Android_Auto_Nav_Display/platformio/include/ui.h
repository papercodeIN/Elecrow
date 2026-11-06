#pragma once
// ===========================================================================
// ui.h - nav card rendering on the ILI9341 (320x240 landscape)
//
// The UI keeps a copy of the last drawn frame and redraws only changed
// regions (no full framebuffer - the board has no PSRAM).
//
// Gestures:
//   single tap  -> cycle backlight brightness
//   double tap  -> toggle day/night theme
// ===========================================================================

#include <stdint.h>
#include "nav_state.h"

void uiInit();

// Feed one navigation update (called for MSG_NAV / MSG_STATE messages).
void uiSetNav(const NavData& nav);

// Mark the link alive without new data (MSG_PING); resets the
// signal-lost timer.
void uiNoteActivity();

// BLE link state (drives the waiting/connected screens).
void uiSetLink(bool connected);

// Call every loop iteration; handles the TURN NOW flash, gestures and
// signal-lost banner (the UI timestamps the last nav update itself).
void uiTick(uint32_t now);
