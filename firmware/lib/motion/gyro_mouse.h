// gyro_mouse - turn phone rotation into relative mouse movement, plus one
// short left click per recognised swing. Hardware-free.
//
//   dx = xSign * (rotation rate about "up")   turn left  -> cursor left
//   dy = ySign * (pitch rate, gyro x)         nose up    -> cursor up
//
// "Up" is the gravity direction, so a left/right turn moves the cursor
// horizontally whether the phone is held flat or upright.
//
// Fractional movement is accumulated, so slow turns still add up instead of
// being rounded away.
//
// The click is NOT speed based: a stroke is signalled by the gesture detector
// via triggerClick(), which starts one bounded press/hold/release. A second
// trigger during a pulse is ignored and counted, so a burst of swings cannot
// leave the button stuck down or produce a surprise click later.
#pragma once

#include <stdint.h>

#include "config.h"
#include "vec3.h"

struct MouseSettings {
    float    gainCountsPerRad;
    float    rateDeadzoneRadS;
    float    xSign, ySign;
    uint32_t clickHoldMs;   // press duration for one recognised swing
    int16_t  maxStep;       // largest dx/dy returned by one take()
    int32_t  maxBacklog;    // pending movement is capped at +-this

    static MouseSettings fromConfig() {
        return {MOUSE_GAIN_COUNTS_PER_RAD, MOUSE_RATE_DEADZONE_RAD_S, MOUSE_X_SIGN,
                MOUSE_Y_SIGN,             SWING_CLICK_HOLD_MS,        MOUSE_MAX_STEP,
                MOUSE_MAX_BACKLOG};
    }
};

struct MouseReport {
    int16_t dx = 0, dy = 0;
    bool    pressed = false;   // desired state of the left button
};

class GyroMouse {
public:
    // `gyro` must already have the bias removed; `up` is the unit gravity
    // direction in the device frame.
    void feed(const Vec3 &gyro, const Vec3 &up, float dtS, const MouseSettings &s);

    // Start one click for a recognised swing. Ignored (and counted) while a
    // pulse is already active.
    void triggerClick(uint32_t nowMs, const MouseSettings &s);
    // Cancel any pulse and button state immediately (safety release).
    void clearClick();

    // Movement accumulated since the last take(), limited to +-maxStep per
    // axis (the rest stays queued), plus the current button state. Advances
    // the pulse timer, so it must be called even when idle.
    MouseReport take(const MouseSettings &s, uint32_t nowMs);

    // Drop all movement and the click (stream lost, recalibrating...).
    void reset();

    bool    pressed() const { return pressed_; }
    bool    pulseActive() const { return pressed_; }
    uint32_t ignoredTriggers() const { return ignoredTriggers_; }

private:
    void updatePulse(uint32_t nowMs, uint32_t holdMs);

    float    fracX_ = 0.0f, fracY_ = 0.0f;   // sub-count remainders
    int32_t  pendX_ = 0, pendY_ = 0;         // whole counts waiting to be sent
    bool     pressed_ = false;
    uint32_t pressStartedMs_ = 0;
    uint32_t ignoredTriggers_ = 0;
};