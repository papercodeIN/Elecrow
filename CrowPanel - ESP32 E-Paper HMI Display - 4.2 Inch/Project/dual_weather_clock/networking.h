#pragma once
#include <WiFi.h>
#include "config.h"

#define WIFI_MAX_RETRIES 8
#define WIFI_BASE_DELAY_MS 500
#define WIFI_MAX_DELAY_MS 16000
#define WIFI_CONNECT_TIMEOUT_MS 15000

inline bool connectWiFiWithBackoff(uint8_t maxRetries) {
    uint32_t retryDelay = WIFI_BASE_DELAY_MS;
    for (uint8_t attempt = 0; attempt < maxRetries; attempt++) {
        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
            delay(retryDelay);
        }
        if (WiFi.status() == WL_CONNECTED) {
            return true;
        }
        retryDelay *= 2;
        if (retryDelay > WIFI_MAX_DELAY_MS) {
            retryDelay = WIFI_MAX_DELAY_MS;
        }
    }
    return false;
}

inline void ensureWiFiConnected() {
    if (WiFi.status() != WL_CONNECTED) {
        connectWiFiWithBackoff(WIFI_MAX_RETRIES);
    }
}
