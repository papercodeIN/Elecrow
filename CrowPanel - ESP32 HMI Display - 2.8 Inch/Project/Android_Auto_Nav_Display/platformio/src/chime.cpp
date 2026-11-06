// ===========================================================================
// chime.cpp - non-blocking beep sequences on the speaker pin
// ===========================================================================

#include <Arduino.h>
#include "chime.h"
#include "config.h"

namespace {

struct Step {
    uint16_t onMs;
    uint16_t offMs;
};

constexpr Step kNearSteps[]   = { { 140, 80 }, { 0, 0 } };
constexpr Step kNowSteps[]    = { { 120, 90 }, { 120, 90 }, { 120, 0 } };
constexpr Step kArriveSteps[] = { { 500, 0 } };

bool       s_enabled = true;
bool       s_playing = false;
bool       s_toneOn  = false;
uint16_t   s_freq    = 2000;
uint8_t    s_idx     = 0;
const Step* s_steps  = nullptr;
uint32_t   s_mark    = 0;

void toneOn() {
    ledcWrite(LEDC_CH_SPEAKER, 1 << (LEDC_SPK_RES - 1));  // 50% duty
}

void toneOff() {
    ledcWrite(LEDC_CH_SPEAKER, 0);
}

void startPattern(uint16_t freq, const Step* steps) {
    if (!s_enabled || !steps || steps[0].onMs == 0) {
        return;
    }
    s_freq   = freq;
    s_steps  = steps;
    s_idx    = 0;
    s_mark   = millis();
    s_playing = true;
    s_toneOn = false;
    ledcSetup(LEDC_CH_SPEAKER, s_freq, LEDC_SPK_RES);
}

void tickPattern(uint32_t now) {
    if (!s_playing) {
        return;
    }
    const Step& step = s_steps[s_idx];
    if (s_toneOn) {
        if (now - s_mark >= step.onMs) {
            toneOff();
            s_toneOn = false;
            s_mark = now;
            if (step.offMs == 0) {
                // Last step of the pattern.
                s_playing = false;
            }
        }
    } else {
        if (now - s_mark >= step.offMs) {
            s_idx++;
            if (s_steps[s_idx].onMs == 0) {
                s_playing = false;
                return;
            }
            s_mark = now;
            toneOn();
            s_toneOn = true;
        }
    }
}

}  // namespace

void chimeInit() {
    ledcSetup(LEDC_CH_SPEAKER, 2000, LEDC_SPK_RES);
    ledcAttachPin(PIN_SPEAKER, LEDC_CH_SPEAKER);
    toneOff();
}

void chimeTick(uint32_t now) {
    tickPattern(now);
}

void chimeStartUrgency(Urgency level) {
    switch (level) {
        case URG_NEAR:
            startPattern(kChimeNearFreq, kNearSteps);
            break;
        case URG_NOW:
            startPattern(kChimeNowFreq, kNowSteps);
            break;
        default:
            break;
    }
}

void chimeStartArrive() {
    startPattern(kChimeArriveFreq, kArriveSteps);
}

void chimeSetEnabled(bool enabled) {
    s_enabled = enabled;
    if (!enabled) {
        chimeStop();
    }
}

bool chimeIsEnabled() {
    return s_enabled;
}

void chimeStop() {
    s_playing = false;
    toneOff();
}
