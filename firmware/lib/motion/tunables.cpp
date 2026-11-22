#include "tunables.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "config.h"

Tunables Tunables::defaults() {
    Tunables t;
    t.onsetRadS = SWING_ONSET_RAD_S;
    t.endRadS = SWING_END_RAD_S;
    t.endHoldMs = static_cast<float>(SWING_END_HOLD_MS);
    t.postPeakMs = static_cast<float>(SWING_POST_PEAK_MS);
    t.maxSwingMs = static_cast<float>(SWING_MAX_MS);
    t.cooldownMs = static_cast<float>(SWING_COOLDOWN_MS);
    t.minIntegralRad = SWING_MIN_INTEGRAL_RAD;
    t.dominanceRatio = SWING_DOMINANCE_RATIO;
    t.tennisMinPeakRadS = TENNIS_MIN_PEAK_RAD_S;
    t.lobMinUpAccel = LOB_MIN_UP_ACCEL_MS2;
    t.serveOnsetPitchDeg = SERVE_ONSET_PITCH_MIN_DEG;
    t.forehandYawSign = FOREHAND_YAW_SIGN;
    t.maxOnsetTiltDeg = SWING_MAX_ONSET_TILT_DEG;
    t.defensiveMaxPeakRadS = CRICKET_DEFENSIVE_MAX_PEAK_RAD_S;
    t.defensiveMaxMs = static_cast<float>(CRICKET_DEFENSIVE_MAX_MS);
    t.loftMinUpAccel = CRICKET_LOFT_MIN_UP_ACCEL_MS2;
    t.pullBothSigns = CRICKET_PULL_BOTH_SIGNS;
    return t;
}

const TunableField TUNABLE_FIELDS[] = {
    {"onset", &Tunables::onsetRadS, 0.3f, 20.0f},
    {"end", &Tunables::endRadS, 0.1f, 20.0f},
    {"end_hold_ms", &Tunables::endHoldMs, 0.0f, 500.0f},
    {"post_peak_ms", &Tunables::postPeakMs, 10.0f, 1000.0f},
    {"max_ms", &Tunables::maxSwingMs, 50.0f, 3000.0f},
    {"cooldown_ms", &Tunables::cooldownMs, 0.0f, 5000.0f},
    {"min_integral", &Tunables::minIntegralRad, 0.05f, 6.0f},
    {"dominance", &Tunables::dominanceRatio, 1.0f, 10.0f},
    {"min_peak", &Tunables::tennisMinPeakRadS, 0.5f, 30.0f},
    {"lob_up_accel", &Tunables::lobMinUpAccel, 0.0f, 60.0f},
    {"serve_pitch", &Tunables::serveOnsetPitchDeg, 0.0f, 89.0f},
    {"yaw_sign", &Tunables::forehandYawSign, -1.0f, 1.0f},
    {"max_tilt", &Tunables::maxOnsetTiltDeg, 10.0f, 89.0f},
    {"defensive_peak", &Tunables::defensiveMaxPeakRadS, 0.5f, 30.0f},
    {"defensive_ms", &Tunables::defensiveMaxMs, 20.0f, 1000.0f},
    {"loft_up_accel", &Tunables::loftMinUpAccel, 0.0f, 60.0f},
    {"pull_both", &Tunables::pullBothSigns, 0.0f, 1.0f},
};

const size_t TUNABLE_FIELD_COUNT = sizeof(TUNABLE_FIELDS) / sizeof(TUNABLE_FIELDS[0]);

static const TunableField *findField(const char *key) {
    if (!key) return nullptr;
    for (size_t i = 0; i < TUNABLE_FIELD_COUNT; i++) {
        if (!strcmp(TUNABLE_FIELDS[i].key, key)) return &TUNABLE_FIELDS[i];
    }
    return nullptr;
}

const char *tunableErrorText(TunableError e) {
    switch (e) {
        case TunableError::None:              return "";
        case TunableError::UnknownKey:        return "unknown key";
        case TunableError::OutOfRange:        return "out of range";
        case TunableError::NotFinite:         return "not a finite number";
        case TunableError::EndNotBelowOnset:  return "end must be below onset";
        case TunableError::PostPeakExceedsMax: return "post_peak_ms must be below max_ms";
        case TunableError::ServeAboveTilt:    return "serve_pitch must be below max_tilt";
        case TunableError::YawSignNotUnit:    return "yaw_sign must be +1 or -1";
    }
    return "?";
}

bool tunablesSet(Tunables &t, const char *key, float value, TunableError *err) {
    if (err) *err = TunableError::None;
    const TunableField *f = findField(key);
    if (!f) {
        if (err) *err = TunableError::UnknownKey;
        return false;
    }
    if (!isfinite(value)) {
        if (err) *err = TunableError::NotFinite;
        return false;
    }
    if (value < f->minValue || value > f->maxValue) {
        if (err) *err = TunableError::OutOfRange;
        return false;
    }
    t.*(f->member) = value;
    return true;
}

bool tunablesGet(const Tunables &t, const char *key, float &value) {
    const TunableField *f = findField(key);
    if (!f) return false;
    value = t.*(f->member);
    return true;
}

TunableError validate(const Tunables &t) {
    // Every field must be finite and inside its own range first.
    for (size_t i = 0; i < TUNABLE_FIELD_COUNT; i++) {
        const float v = t.*(TUNABLE_FIELDS[i].member);
        if (!isfinite(v)) return TunableError::NotFinite;
        if (v < TUNABLE_FIELDS[i].minValue || v > TUNABLE_FIELDS[i].maxValue) {
            return TunableError::OutOfRange;
        }
    }
    // Relations that a per-field check cannot catch.
    if (!(t.endRadS < t.onsetRadS)) return TunableError::EndNotBelowOnset;
    if (!(t.postPeakMs < t.maxSwingMs)) return TunableError::PostPeakExceedsMax;
    if (!(t.serveOnsetPitchDeg < t.maxOnsetTiltDeg)) return TunableError::ServeAboveTilt;
    // A sign of 0 would make every forehand ambiguous.
    if (fabsf(fabsf(t.forehandYawSign) - 1.0f) > 1e-6f) return TunableError::YawSignNotUnit;
    return TunableError::None;
}

bool applyValidated(Tunables &target, const Tunables &candidate, TunableError *err) {
    const TunableError e = validate(candidate);
    if (err) *err = e;
    if (e != TunableError::None) return false;  // target untouched
    target = candidate;
    return true;
}

// Format with enough precision to round-trip, keeping the sign of values
// between -1 and 0 (the previous fixed-point version printed "-0.0" as "0.0").
static void emitValue(char *buf, size_t n, const char *key, float v) {
    // %g drops trailing zeros and switches to exponent form when it helps,
    // which is what strtof needs to give the same float back.
    snprintf(buf, n, "%s=%.4g", key, static_cast<double>(v));
}

void tunablesDump(const Tunables &t, void (*emit)(const char *line)) {
    char buf[64];
    for (size_t i = 0; i < TUNABLE_FIELD_COUNT; i++) {
        emitValue(buf, sizeof(buf), TUNABLE_FIELDS[i].key, t.*(TUNABLE_FIELDS[i].member));
        emit(buf);
    }
}