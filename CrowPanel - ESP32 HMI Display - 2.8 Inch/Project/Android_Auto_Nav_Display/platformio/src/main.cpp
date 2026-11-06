// ===========================================================================
// main.cpp - Android Auto-style Turn-by-Turn Navigation Display
//
// Elecrow CrowPanel 2.8" ESP32 (CYD). Receives Google Maps turn-by-turn data
// from the phone over BLE (Nordic UART Service, JSON lines) and renders a
// nav card with locally-computed urgency prompts (highlight, flash, chime).
//
// Modules: ble_link (transport) - nav_state (parser/model) - ui (rendering)
//          chime (speaker beeps). See the project README for the protocol.
// ===========================================================================

#include <Arduino.h>
#include "config.h"
#include "ble_link.h"
#include "nav_state.h"
#include "ui.h"
#include "chime.h"

static NavData s_nav;                 // currently displayed data
static char    s_line[BLE_LINE_MAX];

static Urgency  s_prevUrg  = URG_NONE;
static NavPhase s_prevPhase = PH_IDLE;
static bool     s_prevLink = false;

static void handleNav(const NavData& incoming) {
    Urgency prevUrg = urgencyFor(s_nav.d, s_nav.phase);
    if (s_prevPhase != PH_NAV) {
        prevUrg = URG_NONE;  // fresh leg: no phantom chime from stale data
    }
    NavPhase prevPhase = s_nav.valid ? s_nav.phase : PH_IDLE;

    s_nav = incoming;
    uiSetNav(s_nav);

    Urgency newUrg = urgencyFor(s_nav.d, s_nav.phase);
    if (s_nav.phase == PH_NAV) {
        if ((int)newUrg > (int)prevUrg) {
            chimeStartUrgency(newUrg);
        }
        s_prevUrg = newUrg;
    } else {
        s_prevUrg = URG_NONE;
        if (s_nav.phase == PH_ARRIVED && prevPhase != PH_ARRIVED) {
            chimeStartArrive();
        }
    }
    s_prevPhase = s_nav.phase;

    Serial.printf("[nav] m=%s d=%ld ds='%s' st='%s' phase=%u\n",
                  maneuverCode(s_nav.m), (long)s_nav.d, s_nav.ds, s_nav.street,
                  (unsigned)s_nav.phase);
}

static void handleState(const NavData& incoming) {
    if (s_nav.valid) {
        NavPhase prevPhase = s_nav.phase;
        s_nav.phase = incoming.phase;
        uiSetNav(s_nav);
        if (incoming.phase != PH_NAV) {
            s_prevUrg = URG_NONE;
        }
        if (incoming.phase == PH_ARRIVED && prevPhase != PH_ARRIVED) {
            chimeStartArrive();
        }
        s_prevPhase = incoming.phase;
        Serial.printf("[nav] state -> %u\n", (unsigned)incoming.phase);
    }
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n=== AA Nav Display ===");

    chimeInit();
    uiInit();
    bleLinkInit(BLE_DEVICE_NAME);

    s_nav.clear();
    Serial.println("[main] ready - waiting for phone");
}

void loop() {
    uint32_t now = millis();

    // ---- BLE link transitions -------------------------------------------
    bool link = bleIsConnected();
    if (link != s_prevLink) {
        s_prevLink = link;
        if (!link) {
            // Lost the phone: drop nav data, back to the waiting screen.
            s_nav.clear();
            s_prevUrg = URG_NONE;
            s_prevPhase = PH_IDLE;
            chimeStop();
        }
        uiSetLink(link);
    }

    // ---- Incoming JSON lines --------------------------------------------
    if (bleHasLine()) {
        size_t n = bleTakeLine(s_line, sizeof(s_line));
        if (n > 0) {
            NavData incoming;
            incoming.clear();
            MsgType mt = navParseLine(s_line, incoming);
            switch (mt) {
                case MSG_NAV:
                    handleNav(incoming);
                    bleSendStatus("{\"t\":\"ack\"}");
                    break;
                case MSG_STATE:
                    handleState(incoming);
                    bleSendStatus("{\"t\":\"ack\"}");
                    break;
                case MSG_PING:
                    uiNoteActivity();
                    break;
                default:
                    Serial.printf("[main] unhandled line (%u bytes)\n", (unsigned)n);
                    break;
            }
        }
    }

    // ---- Rendering + audio ----------------------------------------------
    uiTick(now);
    chimeTick(now);

    delay(4);
}
