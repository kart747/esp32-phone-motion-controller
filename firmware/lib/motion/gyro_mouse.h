// gyro_mouse - turn phone rotation into relative mouse movement and an
// auto-click. Hardware-free.
//
//   dx = xSign * (rotation rate about "up")   turn left  -> cursor left
//   dy = ySign * (pitch rate, gyro x)         nose up    -> cursor up
//
// "Up" is the gravity direction, so a left/right turn moves the cursor
// horizontally whether the phone is held flat or upright.
//
// Fractional movement is accumulated, so slow turns still add up instead of
// being rounded away. The click is held while the phone turns fast and is
// released after it has been slow for a hold time (hysteresis, no flicker).
#pragma once

#include <stdint.h>

#include "vec3.h"

struct MouseSettings {
    float    gainCountsPerRad;
    float    rateDeadzoneRadS;
    float    xSign, ySign;
    float    clickOnRadS, clickOffRadS;
    uint32_t clickReleaseHoldMs;
    int16_t  maxStep;       // largest dx/dy returned by one take()
    int32_t  maxBacklog;    // pending movement is capped at +-this
};

struct MouseReport {
    int16_t dx = 0, dy = 0;
    bool    pressed = false;   // desired state of the left button
};

class GyroMouse {
public:
    // `gyro` must already have the bias removed; `up` is the unit gravity
    // direction in the device frame.
    void feed(const Vec3 &gyro, const Vec3 &up, float dtS, uint32_t nowMs, const MouseSettings &s);

    // Movement accumulated since the last take(), limited to +-maxStep per
    // axis (the rest stays queued), plus the current click state.
    MouseReport take(const MouseSettings &s);

    // Drop all movement and release the click (stream lost, recalibrating...).
    void reset();

    bool pressed() const { return pressed_; }

private:
    float   fracX_ = 0.0f, fracY_ = 0.0f;   // sub-count remainders
    int32_t pendX_ = 0, pendY_ = 0;          // whole counts waiting to be sent
    bool    pressed_ = false;
    bool    slow_ = false;
    uint32_t slowSinceMs_ = 0;
};
