// ===========================================================================
// nav_state.cpp - JSON parsing (ArduinoJson v6) and urgency logic
// ===========================================================================

#include <Arduino.h>
#include <ArduinoJson.h>
#include "nav_state.h"
#include "config.h"

void NavData::clear() {
    m = MNV_UNKNOWN;
    d = -1;
    phase = PH_IDLE;
    speedKmh = 0;
    ds[0] = '\0';
    street[0] = '\0';
    then[0] = '\0';
    eta[0] = '\0';
    route[0] = '\0';
    valid = false;
}

Urgency urgencyFor(int32_t d, NavPhase phase) {
    if (phase != PH_NAV || d < 0) {
        return URG_NONE;
    }
    if (d <= URGENT_NOW_M) {
        return URG_NOW;
    }
    if (d <= URGENT_NEAR_M) {
        return URG_NEAR;
    }
    return URG_NONE;
}

const char* maneuverCode(Maneuver m) {
    switch (m) {
        case MNV_ST:  return "ST";
        case MNV_TL:  return "TL";
        case MNV_TR:  return "TR";
        case MNV_SL:  return "SL";
        case MNV_SR:  return "SR";
        case MNV_SHL: return "SHL";
        case MNV_SHR: return "SHR";
        case MNV_UT:  return "UT";
        case MNV_MEL: return "MEL";
        case MNV_MER: return "MER";
        case MNV_FKL: return "FKL";
        case MNV_FKR: return "FKR";
        case MNV_RAB: return "RAB";
        case MNV_EXL: return "EXL";
        case MNV_EXR: return "EXR";
        case MNV_ARR: return "ARR";
        default:      return "?";
    }
}

static NavPhase phaseFromString(const char* s) {
    if (!s || !*s)          return PH_NAV;  // absent on old senders = navigating
    if (!strcmp(s, "nav"))  return PH_NAV;
    if (!strcmp(s, "reroute")) return PH_REROUTE;
    if (!strcmp(s, "arrived")) return PH_ARRIVED;
    if (!strcmp(s, "idle")) return PH_IDLE;
    return PH_NAV;
}

static Maneuver maneuverFromString(const char* s) {
    if (!s) return MNV_UNKNOWN;
    static const struct { const char* code; Maneuver m; } kMap[] = {
        { "ST",  MNV_ST  }, { "TL",  MNV_TL  }, { "TR",  MNV_TR  },
        { "SL",  MNV_SL  }, { "SR",  MNV_SR  }, { "SHL", MNV_SHL },
        { "SHR", MNV_SHR }, { "UT",  MNV_UT  }, { "MEL", MNV_MEL },
        { "MER", MNV_MER }, { "FKL", MNV_FKL }, { "FKR", MNV_FKR },
        { "RAB", MNV_RAB }, { "EXL", MNV_EXL }, { "EXR", MNV_EXR },
        { "ARR", MNV_ARR },
    };
    for (size_t i = 0; i < sizeof(kMap) / sizeof(kMap[0]); i++) {
        if (!strcmp(kMap[i].code, s)) {
            return kMap[i].m;
        }
    }
    return MNV_UNKNOWN;
}

MsgType navParseLine(const char* line, NavData& out) {
    if (!line || !*line) {
        return MSG_NONE;
    }

    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, line, strlen(line));
    if (err) {
        Serial.printf("[nav] JSON error: %s\n", err.c_str());
        return MSG_NONE;
    }

    const char* t = doc["t"] | "";
    if (!strcmp(t, "ping")) {
        return MSG_PING;
    }

    if (!strcmp(t, "state")) {
        out.phase = phaseFromString(doc["s"] | "");
        return MSG_STATE;
    }

    if (!strcmp(t, "nav")) {
        out.m = maneuverFromString(doc["m"] | "");
        out.d = doc["d"] | (int32_t)-1;
        out.phase = phaseFromString(doc["s"] | "nav");
        out.speedKmh = (uint8_t)(doc["v"] | 0);

        // strlcpy keeps buffers null-terminated and truncates safely.
        strlcpy(out.ds,     doc["ds"] | "",  sizeof(out.ds));
        strlcpy(out.street, doc["st"] | "",  sizeof(out.street));
        strlcpy(out.then,   doc["th"] | "",  sizeof(out.then));
        strlcpy(out.eta,    doc["eta"] | "", sizeof(out.eta));
        strlcpy(out.route,  doc["rt"] | "",  sizeof(out.route));
        out.valid = true;
        return MSG_NAV;
    }

    return MSG_NONE;
}
