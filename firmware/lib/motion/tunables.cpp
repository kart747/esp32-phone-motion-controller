#include "tunables.h"

#include <math.h>
#include <string.h>

#include "config.h"

Tunables Tunables::defaults() {
    Tunables t{};
    t.filterTauS = FILTER_TAU_S;
    t.accelTrustTol = ACCEL_TRUST_TOL_MS2;
    t.onsetRadS = SWING_ONSET_RAD_S;
    t.endRadS = SWING_END_RAD_S;
    t.endHoldMs = SWING_END_HOLD_MS;
    t.postPeakMs = SWING_POST_PEAK_MS;
    t.maxSwingMs = SWING_MAX_MS;
    t.cooldownMs = SWING_COOLDOWN_MS;
    t.minIntegralRad = SWING_MIN_INTEGRAL_RAD;
    t.dominanceRatio = SWING_DOMINANCE_RATIO;
    t.tennisMinPeakRadS = TENNIS_MIN_PEAK_RAD_S;
    t.lobMinUpAccel = LOB_MIN_UP_ACCEL_MS2;
    t.serveOnsetPitchDeg = SERVE_ONSET_PITCH_MIN_DEG;
    t.forehandYawSign = FOREHAND_YAW_SIGN;
    t.defensiveMaxPeakRadS = DEFENSIVE_MAX_PEAK_RAD_S;
    t.defensiveMaxMs = DEFENSIVE_MAX_MS;
    t.loftMinUpAccel = LOFT_MIN_UP_ACCEL_MS2;
    t.pullBothSigns = PULL_BOTH_SIGNS;
    t.stickDeadzoneDeg = STICK_DEADZONE_DEG;
    t.stickFullscaleDeg = STICK_FULLSCALE_DEG;
    t.stickXSign = STICK_X_SIGN;
    t.stickYSign = STICK_Y_SIGN;
    return t;
}

// Allowed ranges are sanity limits (reject typos like a 0 ms cooldown or a
// negative threshold), not recommended values.
const TunableField TUNABLE_FIELDS[] = {
    {"tau",        &Tunables::filterTauS,           0.05f, 10.0f},
    {"trust_tol",  &Tunables::accelTrustTol,        0.1f,  10.0f},
    {"onset",      &Tunables::onsetRadS,            0.2f,  30.0f},
    {"end",        &Tunables::endRadS,              0.1f,  30.0f},
    {"end_hold",   &Tunables::endHoldMs,            5.0f,  500.0f},
    {"post_peak",  &Tunables::postPeakMs,           20.0f, 1000.0f},
    {"max_swing",  &Tunables::maxSwingMs,           100.0f, 2000.0f},
    {"cooldown",   &Tunables::cooldownMs,           50.0f, 3000.0f},
    {"min_int",    &Tunables::minIntegralRad,       0.0f,  10.0f},
    {"dom_ratio",  &Tunables::dominanceRatio,       1.0f,  10.0f},
    {"t_min_peak", &Tunables::tennisMinPeakRadS,    0.2f,  40.0f},
    {"lob_up",     &Tunables::lobMinUpAccel,        0.0f,  100.0f},
    {"serve_pitch",&Tunables::serveOnsetPitchDeg,   0.0f,  90.0f},
    {"fh_sign",    &Tunables::forehandYawSign,      -1.0f, 1.0f},
    {"def_peak",   &Tunables::defensiveMaxPeakRadS, 0.2f,  40.0f},
    {"def_ms",     &Tunables::defensiveMaxMs,       20.0f, 2000.0f},
    {"loft_up",    &Tunables::loftMinUpAccel,       0.0f,  100.0f},
    {"pull_both",  &Tunables::pullBothSigns,        0.0f,  1.0f},
    {"dz_deg",     &Tunables::stickDeadzoneDeg,     0.0f,  45.0f},
    {"full_deg",   &Tunables::stickFullscaleDeg,    5.0f,  90.0f},
    {"x_sign",     &Tunables::stickXSign,           -1.0f, 1.0f},
    {"y_sign",     &Tunables::stickYSign,           -1.0f, 1.0f},
};
const size_t TUNABLE_FIELD_COUNT = sizeof(TUNABLE_FIELDS) / sizeof(TUNABLE_FIELDS[0]);

static const TunableField *findField(const char *key) {
    if (!key) return nullptr;
    for (size_t i = 0; i < TUNABLE_FIELD_COUNT; i++) {
        if (strcmp(TUNABLE_FIELDS[i].key, key) == 0) return &TUNABLE_FIELDS[i];
    }
    return nullptr;
}

bool tunablesSet(Tunables &t, const char *key, float value) {
    const TunableField *f = findField(key);
    if (!f || !isfinite(value) || value < f->minValue || value > f->maxValue) return false;
    t.*(f->member) = value;
    return true;
}

bool tunablesGet(const Tunables &t, const char *key, float &value) {
    const TunableField *f = findField(key);
    if (!f) return false;
    value = t.*(f->member);
    return true;
}
