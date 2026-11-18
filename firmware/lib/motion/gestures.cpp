#include "gestures.h"

#include <math.h>

// Signed distance helper that survives millis() wrap-around.
static inline int32_t since(uint32_t now, uint32_t then) { return static_cast<int32_t>(now - then); }

// --------------------------------------------------------- classification
Gesture classifySwing(GameMode mode, const SwingFeatures &f, const Tunables &t, const char **reason) {
    const char *dummy;
    if (!reason) reason = &dummy;
    *reason = "";

    // --- cricket: a short, low-energy movement is a defensive block. Checked
    //     first because blocks have too little rotation for the axis rules.
    if (mode == GameMode::Cricket && f.peakGyro < t.defensiveMaxPeakRadS &&
        f.durationMs <= static_cast<uint32_t>(t.defensiveMaxMs)) {
        return Gesture::Defensive;
    }
    // --- tennis: ignore weak movements entirely (fidgeting).
    if (mode == GameMode::Tennis && f.peakGyro < t.tennisMinPeakRadS) {
        *reason = "weak";
        return Gesture::None;
    }

    // Dominant rotation axis by |integral| (robust against noisy peaks).
    const float ay = fabsf(f.intYaw), ap = fabsf(f.intPitch), ar = fabsf(f.intRoll);
    char  dom = 'Y';
    float domVal = ay, secondVal = fmaxf(ap, ar);
    if (ap > ay && ap >= ar) {
        dom = 'P'; domVal = ap; secondVal = fmaxf(ay, ar);
    } else if (ar > ay && ar > ap) {
        dom = 'R'; domVal = ar; secondVal = fmaxf(ay, ap);
    }

    if (domVal < t.minIntegralRad) { *reason = "small_integral"; return Gesture::None; }
    if (domVal < t.dominanceRatio * secondVal) { *reason = "ambiguous"; return Gesture::None; }
    if (dom == 'R') { *reason = "roll_dominant"; return Gesture::None; }

    if (dom == 'Y') {
        // Horizontal sweep around the vertical axis.
        if (mode == GameMode::Cricket) {
            const bool positive = f.intYaw * t.forehandYawSign > 0;
            if (positive || t.pullBothSigns >= 0.5f) return Gesture::Pull;
            *reason = "pull_wrong_direction";
            return Gesture::None;
        }
        return (f.intYaw * t.forehandYawSign > 0) ? Gesture::Forehand : Gesture::Backhand;
    }

    // dom == 'P': swing in the vertical plane (nose up / nose down).
    if (f.intPitch > 0) {  // scooping upward
        const float need = mode == GameMode::Tennis ? t.lobMinUpAccel : t.loftMinUpAccel;
        if (f.peakUpAccel >= need) return mode == GameMode::Tennis ? Gesture::Lob : Gesture::Loft;
        *reason = "no_up_accel";
        return Gesture::None;
    }
    // chopping downward
    if (mode == GameMode::Cricket) return Gesture::Drive;
    if (f.onsetPitchDeg >= t.serveOnsetPitchDeg) return Gesture::Serve;  // started raised overhead
    *reason = "downswing_not_overhead";
    return Gesture::None;
}

// ---------------------------------------------------------------- detector
void GestureDetector::reset() {
    state_ = State::Idle;
    quiet_ = false;
    acc_ = SwingFeatures{};
}

void GestureDetector::beginCapture(const GestureInput &in) {
    state_ = State::Capturing;
    onsetMs_ = peakMs_ = in.nowMs;
    quiet_ = false;
    acc_ = SwingFeatures{};
    acc_.onsetPitchDeg = in.pitchDeg;
    acc_.onsetRollDeg = in.rollDeg;
}

bool GestureDetector::update(const GestureInput &in, const Tunables &t, SwingFeatures &out) {
    const float gmag = norm(in.gyro);

    switch (state_) {
        case State::Idle:
            if (gmag >= t.onsetRadS) beginCapture(in);
            else return false;
            break;  // fall through to accumulate this first sample

        case State::Cooldown:
            // Re-arm only after the time is up AND the phone has settled, so
            // the follow-through of the same stroke cannot re-trigger.
            if (since(in.nowMs, cooldownUntilMs_) >= 0 && gmag < t.onsetRadS) state_ = State::Idle;
            return false;

        case State::Capturing:
            break;
    }

    // ---- accumulate features for this sample
    acc_.peakLinAccel = fmaxf(acc_.peakLinAccel, norm(in.linAccel));
    acc_.peakUpAccel = fmaxf(acc_.peakUpAccel, dot(in.linAccel, in.gravityUnit));
    acc_.intYaw += dot(in.gyro, in.gravityUnit) * in.dt;
    acc_.intPitch += in.gyro.x * in.dt;
    acc_.intRoll += in.gyro.y * in.dt;
    if (gmag >= acc_.peakGyro) {
        acc_.peakGyro = gmag;
        peakMs_ = in.nowMs;
    }

    // ---- end-of-capture rules
    if (gmag < t.endRadS) {
        if (!quiet_) { quiet_ = true; quietSinceMs_ = in.nowMs; }
    } else {
        quiet_ = false;
    }
    const bool quietLongEnough = quiet_ && since(in.nowMs, quietSinceMs_) >= static_cast<int32_t>(t.endHoldMs);
    const bool afterPeak = since(in.nowMs, peakMs_) >= static_cast<int32_t>(t.postPeakMs);
    const bool tooLong = since(in.nowMs, onsetMs_) >= static_cast<int32_t>(t.maxSwingMs);
    if (!(quietLongEnough || afterPeak || tooLong)) return false;

    // ---- finish: classify, start cooldown
    acc_.tMs = in.nowMs;
    acc_.durationMs = static_cast<uint32_t>(since(in.nowMs, onsetMs_));
    const char *reason = "";
    acc_.gesture = classifySwing(mode_, acc_, t, &reason);
    acc_.rejectReason = reason;
    out = acc_;

    state_ = State::Cooldown;
    cooldownUntilMs_ = in.nowMs + static_cast<uint32_t>(t.cooldownMs);
    return true;
}
