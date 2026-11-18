// imu_filter - still-pose calibration and a complementary gravity filter.
//
// Conventions (details in docs/axes.md): device frame x = right, y = toward the
// top edge, z = out of the screen. "Gravity vector" below means the UNIT vector
// pointing UP, expressed in the device frame - exactly the direction an
// at-rest accelerometer reads. Neutral pose: phone flat, screen up, top edge
// pointing at the TV, so gravity = (0, 0, 1).
//   roll  > 0 : right edge down        pitch > 0 : nose (top edge) up
#pragma once

#include <stdint.h>

#include "imu_types.h"

// ------------------------------------------------------------ Calibrator
// Collects CAL_DURATION_MS of samples while the phone is held still, then
// yields gyro bias (mean gyro) and the neutral gravity direction (mean accel).
// A window is rejected and restarted automatically if the phone moves, there
// are too few samples, or |accel| does not look like 1 g (wrong units).
class Calibrator {
public:
    enum class State : uint8_t { Idle, Collecting, Done };
    struct Result {
        Vec3  gyroBias;
        Vec3  gravityUnit{0.0f, 0.0f, 1.0f};
        float gravityMag = 9.80665f;
    };

    void start(uint32_t nowMs);
    State update(const ImuSample &s, uint32_t nowMs);
    State state() const { return state_; }
    const Result &result() const { return result_; }
    uint32_t failedAttempts() const { return failed_; }
    const char *lastFailure() const { return lastFailure_; }
    // Progress 0..1 of the current window (for LED/UI).
    float progress(uint32_t nowMs) const;

private:
    void restartWindow(uint32_t nowMs, const char *reason);
    State    state_ = State::Idle;
    Result   result_;
    uint32_t startMs_ = 0, failed_ = 0, n_ = 0;
    double   sumG_[3] = {0, 0, 0}, sumG2_[3] = {0, 0, 0};
    double   sumA_[3] = {0, 0, 0}, sumA2_[3] = {0, 0, 0};
    const char *lastFailure_ = "";
};

// ----------------------------------------------------------- GravityFilter
// Complementary filter on the gravity direction:
//   1. predict: rotate the vector by the gyro (g' = g - w x g * dt)
//   2. correct: blend toward the accelerometer direction with weight
//      dt / (tau + dt), but ONLY while |accel| ~ 1 g. During a swing the
//      accelerometer also measures linear acceleration, so it is ignored and
//      the gyro carries the estimate (standard trick; see tolerance in config).
class GravityFilter {
public:
    void reset(const Vec3 &gravityUnit, float gravityMag);
    // gyro must already have the bias removed.
    void update(const Vec3 &accel, const Vec3 &gyro, float dt, float tauS, float trustTolMs2);

    Vec3  gravity() const { return g_; }
    float gravityMag() const { return mag_; }
    float rollDeg() const { return rollDegFrom(g_); }
    float pitchDeg() const { return pitchDegFrom(g_); }
    bool  lastAccelTrusted() const { return trusted_; }

    static float rollDegFrom(const Vec3 &gravityUnit);
    static float pitchDegFrom(const Vec3 &gravityUnit);

private:
    Vec3  g_{0.0f, 0.0f, 1.0f};
    float mag_ = 9.80665f;
    bool  trusted_ = true;
};
