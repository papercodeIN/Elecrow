#pragma once
// ===========================================================================
// chime.h - non-blocking speaker beeps (GPIO 26 through LEDC PWM)
//
// Square-wave beeps through the on-board speaker connector. Volume is fixed
// (50% duty); patterns are short so they alert without being obnoxious.
// ===========================================================================

#include <stdint.h>
#include "nav_state.h"

void chimeInit();

// Advance the beep state machine; call every loop iteration with millis().
void chimeTick(uint32_t now);

// Fire the pattern for an urgency level change (URG_NEAR / URG_NOW).
void chimeStartUrgency(Urgency level);

// Fire the arrival pattern (used when the phase becomes PH_ARRIVED).
void chimeStartArrive();

void chimeSetEnabled(bool enabled);
bool chimeIsEnabled();
void chimeStop();
