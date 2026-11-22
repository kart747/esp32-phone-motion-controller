#include "gestures.h"

#include <math.h>

#include "config.h"

const char *sportName(Sport s) { return s == Sport::Cricket ? "cricket" : "tennis"; }

const char *gestureName(Gesture g) {
    switch (g) {
        case Gesture::Forehand:  return "forehand";
        case Gesture::Backhand:  return "backhand";
        case Gesture::Lob:       return "lob";
        case Gesture::Serve:     return "serve";
        case Gesture::Drive:     return "drive";
        case Gesture::Pull:      return "pull";
        case Gesture::Loft:      return "loft";
        case Gesture::Defensive: return "defensive";
        default:                 return "none";
    }
}

namespace {

// Signed difference that survives millis() wrap-around.
inline int32_t since(uint32_t now, uint32_t then) { return static_cast<int32_t>(now - then); }

// Component of `v` perpendicular to `up` (the horizontal plane at this pose).
inline Vec3 horizontal(const Vec3 &v, const Vec3 &up) { return v - up * dot(v, up); }

// Orthonormal swing basis from the gravity estimate.
//
// Taking the device X and Y projections independently does NOT give orthogonal
// axes once the phone is tilted (for up = (0.5, 0.5, 0.707) they overlap by
// 1/3, so one rotation would be counted as both pitch and roll). Instead the
// pitch axis is the device X direction flattened into the horizontal plane and
// the roll axis is derived from it, which guarantees orthonormality:
//     pitch = flat(device X)          roll = up x pitch
// Held flat this reduces to (x, y, z) exactly, matching docs/axes.md.
bool onsetAxes(const Vec3 &up, Vec3 &xAxis, Vec3 &yAxis) {
    const Vec3 xh = horizontal({1.0f, 0.0f, 0.0f}, up);
    const float nx = norm(xh);
    xAxis = xh * (1.0f / nx);
    // Roll axis before normalising: its length is sin(angle between up and X).
    const Vec3 yr = cross(up, xAxis);
    const float ny = norm(yr);
    // ny is the cosine of the phone's pitch from flat, so this rejects exactly
    // the poses where the swing plane degenerates.
    if (nx < 1e-3f || ny < 0.2f) return false;
    yAxis = yr * (1.0f / ny);
    return true;
}

}  // namespace

Gesture classifySwing(Sport sport, const SwingFeatures &f, const Tunables &t,
                      const char **reason) {
    const char *dummy;
    if (!reason) reason = &dummy;
    *reason = "";

    if (f.onsetPitchDeg > t.maxOnsetTiltDeg || f.onsetPitchDeg < -t.maxOnsetTiltDeg) {
        *reason = "unsupported_pose";
        return Gesture::None;
    }

    // Cricket defensive: a short, low-energy block. Checked before the axis
    // rules because a block has too little rotation to be dominant on any axis.
    // It still has to clear the shared onset threshold, so movement below that
    // is not detected at all rather than being called a block.
    if (sport == Sport::Cricket && f.peakGyro < t.defensiveMaxPeakRadS &&
        f.durationMs <= static_cast<uint32_t>(t.defensiveMaxMs)) {
        // A very brief movement with almost no net rotation is a fidget, not a
        // block: require the same minimum rotation the other classes use.
        const float domAny = fmaxf(fabsf(f.intYaw), fmaxf(fabsf(f.intPitch), fabsf(f.intRoll)));
        if (domAny < t.minIntegralRad) {
            *reason = "small_integral";
            return Gesture::None;
        }
        return Gesture::Defensive;
    }

    if (f.peakGyro < t.tennisMinPeakRadS) {
        *reason = "weak";
        return Gesture::None;
    }

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
        // Horizontal sweep about the vertical.
        const bool positive = (f.intYaw * t.forehandYawSign > 0);
        if (sport == Sport::Cricket) {
            // A cricket pull can be played to either side, so by default both
            // signs count; a wrist-hook to the wrong side is still a pull.
            if (positive || t.pullBothSigns >= 0.5f) return Gesture::Pull;
            *reason = "pull_wrong_direction";
            return Gesture::None;
        }
        return positive ? Gesture::Forehand : Gesture::Backhand;
    }

    // dom == 'P': swing in the vertical plane.
    if (f.intPitch > 0) {  // scooping upward
        const float need = (sport == Sport::Cricket) ? t.loftMinUpAccel : t.lobMinUpAccel;
        if (f.peakUpAccel >= need) {
            return (sport == Sport::Cricket) ? Gesture::Loft : Gesture::Lob;
        }
        *reason = "no_up_accel";
        return Gesture::None;
    }
    // chopping downward
    if (sport == Sport::Cricket) return Gesture::Drive;
    if (f.onsetPitchDeg >= t.serveOnsetPitchDeg) return Gesture::Serve;
    *reason = "downswing_not_overhead";
    return Gesture::None;
}

void GestureDetector::setSport(Sport s) {
    if (s == sport_) return;
    sport_ = s;
    reset();  // a stroke cannot span a sport change
}

void GestureDetector::reset() {
    state_ = State::Idle;
    quiet_ = false;
    axesValid_ = true;
    acc_ = SwingFeatures{};
}

void GestureDetector::beginCapture(const GestureInput &in) {
    state_ = State::Capturing;
    onsetMs_ = peakMs_ = in.nowMs;
    quiet_ = false;
    acc_ = SwingFeatures{};
    acc_.onsetPitchDeg = in.pitchDeg;
    acc_.onsetRollDeg = in.rollDeg;
    upAtOnset_ = in.up;
    axesValid_ = onsetAxes(upAtOnset_, axisX_, axisY_);
}

bool GestureDetector::update(const GestureInput &in, const Tunables &t, SwingFeatures &out) {
    const float gmag = norm(in.gyro);

    switch (state_) {
        case State::Idle:
            if (gmag >= t.onsetRadS) beginCapture(in);
            else return false;
            break;  // fall through and accumulate this first sample

        case State::Cooldown:
            // Re-arm only when the time is up AND the phone has settled, so the
            // follow-through of the same stroke cannot re-trigger.
            if (since(in.nowMs, cooldownUntilMs_) >= 0 && gmag < t.onsetRadS) {
                state_ = State::Idle;
            }
            return false;

        case State::Capturing:
            break;
    }

    // ---- accumulate features
    const Vec3 linAccel = in.accel - in.up * in.gravityMag;
    acc_.peakLinAccel = fmaxf(acc_.peakLinAccel, norm(linAccel));
    acc_.peakUpAccel = fmaxf(acc_.peakUpAccel, dot(linAccel, in.up));

    if (axesValid_) {
        // Projected on the basis captured at onset, so the three integrals are
        // mutually exclusive for the whole stroke.
        const Vec3 h = horizontal(in.gyro, upAtOnset_);
        acc_.intYaw += dot(in.gyro, upAtOnset_) * in.dt;
        acc_.intPitch += dot(h, axisX_) * in.dt;
        acc_.intRoll += dot(h, axisY_) * in.dt;
    }

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
    const bool quietLongEnough =
        quiet_ && since(in.nowMs, quietSinceMs_) >= static_cast<int32_t>(t.endHoldMs);
    const bool afterPeak = since(in.nowMs, peakMs_) >= static_cast<int32_t>(t.postPeakMs);
    const bool tooLong = since(in.nowMs, onsetMs_) >= static_cast<int32_t>(t.maxSwingMs);
    if (!(quietLongEnough || afterPeak || tooLong)) return false;

    // ---- finish: classify, start cooldown
    acc_.tMs = in.nowMs;
    acc_.durationMs = static_cast<uint32_t>(since(in.nowMs, onsetMs_));
    if (!axesValid_) {
        acc_.gesture = Gesture::None;
        acc_.rejectReason = "unsupported_pose";
    } else {
        const char *reason = "";
        acc_.gesture = classifySwing(sport_, acc_, t, &reason);
        acc_.rejectReason = reason;
    }
    out = acc_;

    state_ = State::Cooldown;
    cooldownUntilMs_ = in.nowMs + static_cast<uint32_t>(t.cooldownMs);
    return true;
}