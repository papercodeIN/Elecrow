// ===========================================================================
// ble_link.cpp - Nordic UART Service GATT server with line reassembly
// ===========================================================================

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLESecurity.h>
#include <BLE2902.h>
#include "ble_link.h"
#include "config.h"

// ----------------------------- State --------------------------------------
static BLEServer*        s_server     = nullptr;
static BLECharacteristic* s_txChar    = nullptr;
static bool              s_connected  = false;
static bool              s_stackReady = false;

// Line reassembly: onWrite() runs in the BLE task, bleTakeLine() in the main
// loop, so buffer indices are guarded by a spinlock critical section.
static portMUX_TYPE      s_mux        = portMUX_INITIALIZER_UNLOCKED;
static char              s_fragBuf[BLE_LINE_MAX];
static size_t            s_fragLen    = 0;
static char              s_readyBuf[BLE_LINE_MAX];
static size_t            s_readyLen   = 0;
static bool              s_lineReady  = false;

// ----------------------------- Callbacks ----------------------------------
class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* server) override {
        s_connected = true;
        Serial.println("[ble] GATT connected");
    }

    void onDisconnect(BLEServer* server) override {
        s_connected = false;
        Serial.println("[ble] GATT disconnected - advertising again");
        // Re-advertise immediately; the phone also auto-reconnects to bonded
        // devices, and keeping bonds intact is what makes that reliable.
        if (s_server) {
            s_server->getAdvertising()->start();
        }
    }
};

class RxCallbacks : public BLECharacteristicCallbacks {
    // The library dispatches this two-arg variant; the one-arg overload is
    // only called when this one is not overridden.
    void onWrite(BLECharacteristic* pChar, esp_ble_gatts_cb_param_t* param) override {
        (void)param;
        std::string value = pChar->getValue();
        size_t len = value.length();
        if (len == 0) {
            return;
        }
        const char* data = value.data();

        portENTER_CRITICAL(&s_mux);
        for (size_t i = 0; i < len; i++) {
            char c = data[i];
            if (c == '\n' || c == '\r') {
                if (s_fragLen > 0 && !s_lineReady) {
                    s_fragBuf[s_fragLen] = '\0';
                    memcpy(s_readyBuf, s_fragBuf, s_fragLen + 1);
                    s_readyLen = s_fragLen;
                    s_lineReady = true;
                    s_fragLen = 0;
                }
                continue;
            }
            if (s_fragLen < BLE_LINE_MAX - 1) {
                s_fragBuf[s_fragLen++] = c;
            }
            // Overlong lines are dropped silently: the buffer resets at the
            // next newline so the stream stays in sync.
        }
        portEXIT_CRITICAL(&s_mux);
    }
};

// ----------------------------- API ----------------------------------------
void bleLinkInit(const char* deviceName) {
    BLEDevice::init(deviceName);
    BLEDevice::setMTU(185);  // let a full JSON line fit in one write

    // Just Works pairing with bonding; see header for the rationale.
    BLESecurity* sec = new BLESecurity();
    sec->setAuthenticationMode(ESP_LE_AUTH_BOND);
    sec->setCapability(ESP_IO_CAP_NONE);
    sec->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    sec->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

    s_server = BLEDevice::createServer();
    s_server->setCallbacks(new ServerCallbacks());

    BLEService* service = s_server->createService(BLE_SERVICE_UUID);

    BLECharacteristic* rx = service->createCharacteristic(
        BLE_CHAR_RX_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    rx->setCallbacks(new RxCallbacks());

    s_txChar = service->createCharacteristic(
        BLE_CHAR_TX_UUID,
        BLECharacteristic::PROPERTY_NOTIFY);
    s_txChar->addDescriptor(new BLE2902());

    service->start();

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(BLE_SERVICE_UUID);
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06);  // helps iPhone connection interval issues
    adv->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    s_stackReady = true;
    Serial.printf("[ble] advertising as \"%s\" (%s)\n", deviceName, BLE_SERVICE_UUID);
}

bool bleIsConnected() {
    return s_stackReady && s_connected;
}

bool bleHasLine() {
    bool ready;
    portENTER_CRITICAL(&s_mux);
    ready = s_lineReady;
    portEXIT_CRITICAL(&s_mux);
    return ready;
}

size_t bleTakeLine(char* out, size_t maxLen) {
    size_t n = 0;
    portENTER_CRITICAL(&s_mux);
    if (s_lineReady) {
        n = (s_readyLen < maxLen - 1) ? s_readyLen : maxLen - 1;
        memcpy(out, s_readyBuf, n);
        out[n] = '\0';
        s_lineReady = false;
        s_readyLen = 0;
    }
    portEXIT_CRITICAL(&s_mux);
    return n;
}

void bleSendStatus(const char* json) {
    if (!s_txChar || !s_connected) {
        return;
    }
    s_txChar->setValue((uint8_t*)json, (size_t)strlen(json));
    s_txChar->notify();
    // Small delay so consecutive notifications are not dropped by the stack.
    delay(3);
}
