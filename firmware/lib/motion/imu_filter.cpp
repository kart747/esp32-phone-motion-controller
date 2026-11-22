#include "imu_filter.h"

#include <math.h>

#include "config.h"

static const float RAD_TO_DEG_F = 57.29577951f;

// ------------------------------------------------------------ Calibrator
void Calibrator::start(uint32_t nowMs) {
    // Keep lastFailure_ so the reason for the previous rejection survives the
    // restart; resetFailures() is what clears it.
    state_ = State::Collecting;
    restartWindow(nowMs, nullptr);
}

void Calibrator::noteFailure(const char *reason) {
    failed_++;
    lastFailure_ = reason ? reason : "failed";
}

void Calibrator::resetFailures() {
    failed_ = 0;
    lastFailure_ = "";
}

void Calibrator::restartWindow(uint32_t nowMs, const char *reason) {
    if (reason) {
        failed_++;
        lastFailure_ = reason;
    }
    startMs_ = nowMs;
    n_ = 0;
    for (int i = 0; i < 3; i++) sumG_[i] = sumG2_[i] = sumA_[i] = sumA2_[i] = 0.0;
}

float Calibrator::progress(uint32_t nowMs) const {
    if (state_ == State::Done) return 1.0f;
    if (state_ == State::Idle) return 0.0f;
    const float p = (nowMs - startMs_) / static_cast<float>(CAL_DURATION_MS);
    return p > 1.0f ? 1.0f : p;
}

Calibrator::State Calibrator::update(const ImuSample &s, uint32_t nowMs) {
    if (state_ != State::Collecting) return state_;

    // A clearly moving phone cannot be "still": restart immediately so the
    // user gets a fresh window as soon as they settle.
    if (norm(s.gyro) > CAL_ABORT_GYRO_RAD_S) {
        restartWindow(nowMs, "moving");
        return state_;
    }

    const float g[3] = {s.gyro.x, s.gyro.y, s.gyro.z};
    const float a[3] = {s.accel.x, s.accel.y, s.accel.z};
    for (int i = 0; i < 3; i++) {
        sumG_[i] += g[i];
        sumG2_[i] += static_cast<double>(g[i]) * g[i];
        sumA_[i] += a[i];
        sumA2_[i] += static_cast<double>(a[i]) * a[i];
    }
    n_++;

    if (nowMs - startMs_ < CAL_DURATION_MS) return state_;

    if (n_ < CAL_MIN_SAMPLES) {
        restartWindow(nowMs, "too_few_samples");
        return state_;
    }

    const double n = n_;
    const Vec3 gMean{static_cast<float>(sumG_[0] / n), static_cast<float>(sumG_[1] / n),
                     static_cast<float>(sumG_[2] / n)};
    const Vec3 aMean{static_cast<float>(sumA_[0] / n), static_cast<float>(sumA_[1] / n),
                     static_cast<float>(sumA_[2] / n)};
    double gVar = 0, aVar = 0;
    for (int i = 0; i < 3; i++) {
        const double mg = sumG_[i] / n, ma = sumA_[i] / n;
        gVar += sumG2_[i] / n - mg * mg;
        aVar += sumA2_[i] / n - ma * ma;
    }
    const float gStd = sqrtf(static_cast<float>(gVar > 0 ? gVar : 0));
    const float aStd = sqrtf(static_cast<float>(aVar > 0 ? aVar : 0));
    const float mag = norm(aMean);

    if (gStd > CAL_MAX_GYRO_STD || aStd > CAL_MAX_ACCEL_STD) {
        restartWindow(nowMs, "not_still");
    } else if (mag < CAL_GRAVITY_MIN_MS2 || mag > CAL_GRAVITY_MAX_MS2) {
        restartWindow(nowMs, "gravity_magnitude_wrong_units?");
    } else {
        const Vec3 gUnit = aMean * (1.0f / mag);
        const float pitchRef = GravityFilter::pitchDegFrom(gUnit);
        if (fabsf(pitchRef) > CAL_MAX_REF_TILT_DEG) {
            // Too steep to define roll/pitch relative to, and close to the
            // Euler singularity: ask for a flat phone instead.
            restartWindow(nowMs, "neutral_not_flat");
        } else {
            result_.gyroBias = gMean;
            result_.gravityUnit = gUnit;
            result_.gravityMag = mag;
            result_.pitchRefDeg = pitchRef;
            result_.rollRefDeg = GravityFilter::rollDegFrom(gUnit);
            state_ = State::Done;
        }
    }
    return state_;
}

// ----------------------------------------------------------- GravityFilter
void GravityFilter::reset(const Vec3 &gravityUnit, float gravityMag) {
    g_ = normalized(gravityUnit);
    mag_ = gravityMag;
    trusted_ = true;
}

void GravityFilter::update(const Vec3 &accel, const Vec3 &gyro, float dt, float tauS,
                           float trustTolMs2) {
    // 1. Predict with the gyro: a world-fixed vector seen from a rotating body
    //    frame changes by -w x g.
    g_ = normalized(g_ - cross(gyro, g_) * dt, g_);

    // 2. Correct toward the accelerometer while it mostly measures gravity.
    const float aMag = norm(accel);
    trusted_ = aMag > 1e-3f && fabsf(aMag - mag_) <= trustTolMs2;
    if (trusted_) {
        const float k = dt / (tauS + dt);
        g_ = normalized(g_ * (1.0f - k) + (accel * (1.0f / aMag)) * k, g_);
    }
}

bool GravityFilter::nearSingularity(float pitchDeg) {
    return fabsf(pitchDeg) >= NEAR_SINGULAR_PITCH_DEG;
}

float GravityFilter::rollDegFrom(const Vec3 &g) {
    return atan2f(-g.x, g.z) * RAD_TO_DEG_F;
}

float GravityFilter::pitchDegFrom(const Vec3 &g) {
    return atan2f(g.y, sqrtf(g.x * g.x + g.z * g.z)) * RAD_TO_DEG_F;
}