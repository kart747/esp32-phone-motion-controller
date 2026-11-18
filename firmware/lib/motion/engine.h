// engine - chains calibration -> gravity filter -> stick mapping -> gesture
// detection for one stream of ImuSamples. Hardware-free, so the whole chain
// can be tested on the laptop with synthetic data.
#pragma once

#include <stdint.h>

#include "gestures.h"
#include "imu_filter.h"
#include "imu_types.h"
#include "stick.h"
#include "tunables.h"

struct EngineOutput {
    bool  calibrating = true;     // true until the neutral pose has been captured
    float rollDeg = 0;            // relative to the neutral pose
    float pitchDeg = 0;
    float stickX = 0, stickY = 0; // -1..1 (0 while calibrating)
    bool  swingEnded = false;     // a swing finished this sample (recognised or not)
    SwingFeatures swing;          // valid when swingEnded
};

class MotionEngine {
public:
    void begin(const Tunables &t, GameMode mode);
    void setTunables(const Tunables &t) { tun_ = t; }
    const Tunables &tunables() const { return tun_; }

    void setMode(GameMode m);
    GameMode mode() const { return mode_; }
    void toggleMode() { setMode(mode_ == GameMode::Tennis ? GameMode::Cricket : GameMode::Tennis); }

    // Start a new still-pose calibration at the next sample.
    void recalibrate() { needCal_ = true; }
    bool calibrating() const { return needCal_ || cal_.state() != Calibrator::State::Done; }
    uint32_t calFailures() const { return cal_.failedAttempts(); }
    const char *calLastFailure() const { return cal_.lastFailure(); }
    Vec3 gyroBias() const { return cal_.result().gyroBias; }

    // Process one sample. Always fills `out`.
    void feed(const ImuSample &s, EngineOutput &out);

    // Phone data stopped: abandon any half-captured swing and centre the
    // stick (the calibration itself is kept).
    void onDataLost();

private:
    float computeDt(const ImuSample &s);

    Tunables tun_ = Tunables::defaults();
    GameMode mode_ = GameMode::Tennis;
    Calibrator cal_;
    GravityFilter filter_;
    GestureDetector det_;
    bool  needCal_ = true;
    bool  havePrev_ = false, prevHasTs_ = false;
    double   prevTs_ = 0;
    uint32_t prevRx_ = 0;
    float rollRef_ = 0, pitchRef_ = 0;
};
