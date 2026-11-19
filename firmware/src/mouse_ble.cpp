#include "mouse_ble.h"

#include <Arduino.h>
#include <HijelHID_BLEMouse.h>

#include "config.h"

namespace {
// Created in begin() so nothing touches NimBLE unless mouse mode is selected.
HijelBLEMouse *s_mouse = nullptr;

}  // namespace

bool MouseBle::begin() {
    s_mouse = new HijelBLEMouse(BLE_MOUSE_NAME, BLE_MANUFACTURER, 100, 3);
    s_mouse->begin();
    return true;
}

bool MouseBle::connected() const { return s_mouse && s_mouse->isPaired(); }

void MouseBle::releaseAll(GyroMouse &motion) {
    motion.reset();
    if (s_mouse && buttonDown_) {
        s_mouse->releaseAll();
    }
    buttonDown_ = false;
}

void MouseBle::update(uint32_t nowMs, GyroMouse &motion) {
    if (!s_mouse) return;
    if (nowMs - lastSendMs_ < MOUSE_REPORT_INTERVAL_MS) return;
    lastSendMs_ = nowMs;

    if (!s_mouse->isPaired()) {
        motion.reset();  // nobody is listening: don't queue up a jump for later
        buttonDown_ = false;
        return;
    }

    const MouseReport r = motion.take(MouseSettings::fromConfig());
    if (r.pressed != buttonDown_) {
        buttonDown_ = r.pressed;
        s_mouse->setButton(MouseButton::Left, r.pressed);
    }
    if (r.dx != 0 || r.dy != 0) s_mouse->move(r.dx, r.dy);
}
