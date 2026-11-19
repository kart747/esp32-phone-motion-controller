// mouse_ble - the BLE HID mouse the host sees (HijelHID_BLEMouse over NimBLE).
// Thin adapter so the rest of the firmware never touches the library.
#pragma once

#include <stdint.h>

#include "gyro_mouse.h"

class MouseBle {
public:
    // Start BLE advertising as a HID mouse. Returns false on failure.
    bool begin();

    // True once a host has connected and the link is paired (input is live).
    bool connected() const;

    // Call often. Every MOUSE_REPORT_INTERVAL_MS it takes the queued movement
    // from `motion`, sends it, and updates the left button to match the
    // auto-click state. While no host is paired the movement is discarded.
    void update(uint32_t nowMs, GyroMouse &motion);

    // Safety release: drop queued movement and release every button now.
    void releaseAll(GyroMouse &motion);

private:
    uint32_t lastSendMs_ = 0;
    bool     buttonDown_ = false;
};
