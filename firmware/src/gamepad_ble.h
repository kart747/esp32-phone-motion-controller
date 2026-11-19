// gamepad_ble - the BLE HID gamepad the laptop sees. Thin adapter over
// lemmingDev/ESP32-BLE-Gamepad (NimBLE) so the rest of the firmware never
// touches the library directly.
#pragma once

#include <stdint.h>

class GamepadBle {
public:
    // Start BLE advertising as a HID gamepad. Returns false on failure.
    bool begin();

    // True while a host (the laptop) is connected.
    bool connected() const;

    // Desired stick position in -1..1, already in HID terms (negative Y =
    // up/forward). Sent on the next update().
    void setStick(float x, float y);

    // Safety release: centre the stick, release every button and send that
    // state immediately (if connected).
    void releaseAll();

    // Call often (every loop). Sends a report when the state changed and at
    // least GAMEPAD_REPORT_INTERVAL_MS has passed since the last one.
    void update(uint32_t nowMs);

private:
    float    wantX_ = 0.0f, wantY_ = 0.0f;
    int16_t  sentX_ = 0, sentY_ = 0;
    uint32_t lastSendMs_ = 0;
    bool     forceSend_ = false;
};
