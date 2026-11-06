#pragma once
// ===========================================================================
// ble_link.h - BLE peripheral (Nordic UART Service GATT server)
//
// Bonding policy (learned from the Bluetooth Stream Deck project):
//   - always the ESP32 factory Bluetooth address (never a random one),
//   - Just Works pairing (no MITM), bonds kept forever,
//   - never erase bonds in firmware. Stable auto-reconnect on both sides.
// Characteristics are left open (no encryption gate) so the link works with
// every serial-over-BLE tool; only pair devices you control.
// ===========================================================================

#include <stddef.h>

// Initialise the stack, register the GATT server, start advertising.
void bleLinkInit(const char* deviceName);

bool bleIsConnected();

// True when a complete '\n'-terminated line is waiting.
bool bleHasLine();

// Copy the pending line (without '\n', always null-terminated) into `out`.
// Returns the number of bytes copied (0 if nothing pending or too long).
size_t bleTakeLine(char* out, size_t maxLen);

// Send a status line back to the phone (no '\n' needed). Best effort.
void bleSendStatus(const char* json);
