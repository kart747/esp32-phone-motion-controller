// pipeline - Phase 3 chain: calibration -> gravity filter -> roll/pitch.
//
// Hardware-free on purpose: the whole chain is driven by ImuSamples and the
// arrival timestamp, so it can be tested on the laptop with synthetic data.
#pragma once

#include <stdint.h>

#include "imu_filter.h"
#include "imu_types.h"
#include "sample_clock.h"

struct PipelineOutput {
    bool        calibrating = true;   // true until the neutral pose is captured
    bool        anglesValid = false;  // rollDeg/pitchDeg hold a usable value
    float       rollDeg = 0.0f;       // relative to the calibrated neutral pose
    float       pitchDeg = 0.0f;
    bool        nearSingularity = false;  // roll is ill-defined at this pitch
    bool        calJustCompleted = false;  // this sample finished calibration
    float       calProgress = 0.0f;       // 0..1 while calibrating
    // Valid when !calibrating (used by the gyro mouse and the gesture detector):
    Vec3        gyro;                     // rad/s, bias removed, device frame
    Vec3        up{0.0f, 0.0f, 1.0f};     // unit gravity direction, device frame
    float       gravityMag = 9.80665f;    // |accel| measured while calibrating
    float       dtS = 0.0f;               // seconds since the previous sample
    bool        timingGap = false;        // this sample bridged a real silence
};

class OrientationPipeline {
public:
    void begin();

    // Process one valid sample. Always fills `out`.
    void feed(const ImuSample &s, PipelineOutput &out);

    // The stream stalled: drop any half-captured window and sample timing.
    // A completed calibration is kept (the phone is still in the same pose).
    void onDataLost(uint32_t nowMs);

    void requestRecalibration() {
        needCal_ = true;
        cal_.resetFailures();
    }

    bool calibrated() const { return cal_.state() == Calibrator::State::Done && !needCal_; }
    const Calibrator::Result &calibration() const { return cal_.result(); }
    const char *lastCalFailure() const { return cal_.lastFailure(); }
    uint32_t calFailures() const { return cal_.failedAttempts(); }
    bool calibrating() const { return needCal_ || cal_.state() != Calibrator::State::Done; }

private:
    Calibrator    cal_;
    GravityFilter filter_;
    SampleClock   clock_;
    bool          needCal_ = true;
    float         rollRef_ = 0.0f;
    float         pitchRef_ = 0.0f;
};