#pragma once
// ===========================================================================
// nav_state.h - navigation data model + BLE JSON line parser
//
// Protocol v1 (Nordic UART Service), one JSON object per line:
//   {"t":"nav","m":"TR","d":320,"ds":"320 m","st":"Main St","th":"NH 48",
//    "eta":"14:32","rt":"12.4 km","v":0,"s":"nav"}
//   {"t":"state","s":"reroute"}          nav state change
//   {"t":"ping"}                          heartbeat (every ~5 s)
// See the project README for the full schema and maneuver code table.
// ===========================================================================

#include <stdint.h>
#include <stddef.h>

// ----------------------------- Types --------------------------------------
enum Maneuver : uint8_t {
    MNV_ST,     // continue straight
    MNV_TL, MNV_TR,
    MNV_SL, MNV_SR,
    MNV_SHL, MNV_SHR,
    MNV_UT,
    MNV_MEL, MNV_MER,
    MNV_FKL, MNV_FKR,
    MNV_RAB,    // roundabout
    MNV_EXL, MNV_EXR,
    MNV_ARR,    // arrive at destination
    MNV_COUNT,
    MNV_UNKNOWN = 0xFF
};

enum NavPhase : uint8_t {
    PH_IDLE = 0,     // connected, no active navigation
    PH_NAV,          // navigating
    PH_REROUTE,      // Maps is recalculating
    PH_ARRIVED
};

enum Urgency : uint8_t {
    URG_NONE = 0,    // > URGENT_NEAR_M
    URG_NEAR,        // <= URGENT_NEAR_M  (amber)
    URG_NOW          // <= URGENT_NOW_M   (flashing red)
};

enum MsgType : uint8_t {
    MSG_NONE = 0,    // unparseable / unknown
    MSG_NAV,
    MSG_STATE,
    MSG_PING
};

// ----------------------------- Data ---------------------------------------
struct NavData {
    Maneuver  m;
    int32_t   d;         // meters to maneuver; negative = unknown
    NavPhase  phase;
    uint8_t   speedKmh;  // 0 = not provided
    char      ds[20];    // pre-formatted distance, shown verbatim ("320 m")
    char      street[56];
    char      then[48];
    char      eta[12];
    char      route[16];
    bool      valid;

    void clear();
};

// Urgency level for a given distance (URG_NONE unless actively navigating).
Urgency urgencyFor(int32_t d, NavPhase phase);

// Protocol code ("TR") for a maneuver enum; "?" for unknown.
const char* maneuverCode(Maneuver m);

// Parse one JSON line. Returns the message type; on MSG_NAV/MSG_STATE the
// relevant fields of `out` are filled in (other fields untouched).
MsgType navParseLine(const char* line, NavData& out);
