#include "gamepad_ble.h"

#include <Arduino.h>
#include <BleGamepad.h>

#include "config.h"
#include "stick.h"

namespace {
// Generic HID mode: a plain gamepad with two axes (left stick) and a few
// buttons. The library is constructed once; its report sending is manual so
// we control the rate.
BleGamepad s_pad(BLE_DEVICE_NAME, BLE_MANUFACTURER, 100);
}  // namespace

bool GamepadBle::begin() {
    BleGamepadConfiguration cfg;
    cfg.setAutoReport(false);
    cfg.setButtonCount(GAMEPAD_BUTTON_COUNT);
    cfg.setHatSwitchCount(0);
    cfg.setWhichAxes(true, true, false, false, false, false, false, false);  // X, Y only
    s_pad.begin(&cfg);
    return true;
}

bool GamepadBle::connected() const { return s_pad.isConnected(); }

void GamepadBle::setStick(float x, float y) {
    wantX_ = x;
    wantY_ = y;
}

void GamepadBle::releaseAll() {
    wantX_ = wantY_ = 0.0f;
    forceSend_ = true;
    update(millis());
}

void GamepadBle::update(uint32_t nowMs) {
    if (!s_pad.isConnected()) {
        sentX_ = sentY_ = 0;  // a fresh connection starts from "centred"
        return;
    }
    const int16_t x = axisToHid(wantX_, GAMEPAD_AXIS_MAX);
    const int16_t y = axisToHid(wantY_, GAMEPAD_AXIS_MAX);
    const bool changed = x != sentX_ || y != sentY_;
    if (!forceSend_ && !changed) return;
    if (!forceSend_ && nowMs - lastSendMs_ < GAMEPAD_REPORT_INTERVAL_MS) return;

    if (forceSend_) s_pad.resetButtons();
    s_pad.setLeftThumb(x, y);
    s_pad.sendReport();
    sentX_ = x;
    sentY_ = y;
    lastSendMs_ = nowMs;
    forceSend_ = false;
}
