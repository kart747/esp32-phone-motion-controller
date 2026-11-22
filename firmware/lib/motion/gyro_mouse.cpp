#include "gyro_mouse.h"

#include <math.h>

namespace {

// Soft dead zone: rates inside it are zero, and the output grows from zero at
// the edge (no jump when leaving it).
float applyDeadzone(float rate, float dz) {
    const float mag = fabsf(rate);
    if (!(mag > dz)) return 0.0f;
    return rate < 0.0f ? -(mag - dz) : (mag - dz);
}

int32_t clampI32(int32_t v, int32_t lim) { return v > lim ? lim : (v < -lim ? -lim : v); }

// Move whole counts from the float accumulator to the integer backlog.
void drain(float &frac, int32_t &pend, int32_t backlogLimit) {
    const int32_t whole = static_cast<int32_t>(frac);  // truncates toward zero
    frac -= static_cast<float>(whole);
    pend = clampI32(pend + whole, backlogLimit);
}

}  // namespace

void GyroMouse::feed(const Vec3 &gyro, const Vec3 &up, float dtS, const MouseSettings &s) {
    if (!isfinite(gyro.x) || !isfinite(gyro.y) || !isfinite(gyro.z) || !(dtS > 0.0f)) return;

    const float yawRate = dot(gyro, up);   // turn about the vertical
    const float pitchRate = gyro.x;

    fracX_ += s.xSign * applyDeadzone(yawRate, s.rateDeadzoneRadS) * s.gainCountsPerRad * dtS;
    fracY_ += s.ySign * applyDeadzone(pitchRate, s.rateDeadzoneRadS) * s.gainCountsPerRad * dtS;
    drain(fracX_, pendX_, s.maxBacklog);
    drain(fracY_, pendY_, s.maxBacklog);

    if (!s.speedClick) {
        speedPressed_ = false;
        slowForS_ = 0.0f;
        return;
    }
    // Speed click on the un-deadzoned rates, with hysteresis and a release hold.
    const float speed = sqrtf(yawRate * yawRate + pitchRate * pitchRate);
    if (!speedPressed_) {
        if (speed >= s.clickOnRadS) {
            speedPressed_ = true;
            slowForS_ = 0.0f;
        }
    } else if (speed < s.clickOffRadS) {
        slowForS_ += dtS;
        if (slowForS_ * 1000.0f >= static_cast<float>(s.clickReleaseHoldMs)) {
            speedPressed_ = false;
            slowForS_ = 0.0f;
        }
    } else {
        slowForS_ = 0.0f;  // fast again: restart the release hold
    }
}

void GyroMouse::triggerClick(uint32_t nowMs, const MouseSettings &s) {
    if (!(s.clickHoldMs > 0)) return;
    if (pressed_) {
        ignoredTriggers_++;  // one click per stroke; never queue a later one
        return;
    }
    pressed_ = true;
    pressStartedMs_ = nowMs;
}

void GyroMouse::clearClick() {
    pressed_ = false;
    pressStartedMs_ = 0;
    speedPressed_ = false;
    slowForS_ = 0.0f;
}

void GyroMouse::updatePulse(uint32_t nowMs, uint32_t holdMs) {
    // Unsigned subtraction: correct across the millis() wrap.
    if (pressed_ && (nowMs - pressStartedMs_) >= holdMs) pressed_ = false;
}

MouseReport GyroMouse::take(const MouseSettings &s, uint32_t nowMs) {
    updatePulse(nowMs, s.clickHoldMs);

    MouseReport r;
    const int32_t dx = clampI32(pendX_, s.maxStep);
    const int32_t dy = clampI32(pendY_, s.maxStep);
    pendX_ -= dx;
    pendY_ -= dy;
    r.dx = static_cast<int16_t>(dx);
    r.dy = static_cast<int16_t>(dy);
    r.pressed = pressed_ || speedPressed_;
    return r;
}

void GyroMouse::reset() {
    fracX_ = fracY_ = 0.0f;
    pendX_ = pendY_ = 0;
    pressed_ = false;
    pressStartedMs_ = 0;
    speedPressed_ = false;
    slowForS_ = 0.0f;
}