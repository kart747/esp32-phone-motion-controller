#include "engine.h"

#include "config.h"

void MotionEngine::begin(const Tunables &t, GameMode mode) {
    tun_ = t;
    mode_ = mode;
    det_.setMode(mode);
    needCal_ = true;
    havePrev_ = false;
}

void MotionEngine::setMode(GameMode m) {
    mode_ = m;
    det_.setMode(m);  // also drops any capture in progress
}

void MotionEngine::onDataLost() {
    det_.reset();
    havePrev_ = false;  // first sample after a gap must not produce a huge dt
}

// dt from the phone timestamp when it is sane, else from arrival time, else
// the nominal rate. Wi-Fi jitter makes arrival time a worse source, which is
// why the phone timestamp is preferred.
float MotionEngine::computeDt(const ImuSample &s) {
    float dt = 1.0f / NOMINAL_SAMPLE_RATE_HZ;
    if (havePrev_) {
        float candidate = -1.0f;
        if (s.hasTimestamp && prevHasTs_) candidate = static_cast<float>(s.tsSec - prevTs_);
        if (!(candidate > 0.0f && candidate <= DT_MAX_S)) {
            candidate = (s.rxMs - prevRx_) / 1000.0f;
        }
        if (candidate > 0.0f && candidate <= DT_MAX_S) dt = candidate;
    }
    havePrev_ = true;
    prevHasTs_ = s.hasTimestamp;
    prevTs_ = s.tsSec;
    prevRx_ = s.rxMs;
    return dt;
}

void MotionEngine::feed(const ImuSample &s, EngineOutput &out) {
    out = EngineOutput{};
    const float dt = computeDt(s);

    // ---------------------------------------------------------- calibration
    if (needCal_) {
        cal_.start(s.rxMs);
        needCal_ = false;
        det_.reset();
    }
    if (cal_.state() == Calibrator::State::Collecting) {
        if (cal_.update(s, s.rxMs) == Calibrator::State::Done) {
            const Calibrator::Result &r = cal_.result();
            filter_.reset(r.gravityUnit, r.gravityMag);
            rollRef_ = GravityFilter::rollDegFrom(r.gravityUnit);
            pitchRef_ = GravityFilter::pitchDegFrom(r.gravityUnit);
            det_.reset();
        } else {
            out.calibrating = true;
            return;
        }
    }
    out.calibrating = false;

    // --------------------------------------------------------------- filter
    const Vec3 gyro = s.gyro - cal_.result().gyroBias;
    filter_.update(s.accel, gyro, dt, tun_.filterTauS, tun_.accelTrustTol);
    const Vec3 g = filter_.gravity();
    out.rollDeg = wrapDeg(filter_.rollDeg() - rollRef_);
    out.pitchDeg = wrapDeg(filter_.pitchDeg() - pitchRef_);

    // ---------------------------------------------------------------- stick
    const StickOut st = mapTilt(filter_.rollDeg(), filter_.pitchDeg(), rollRef_, pitchRef_,
                                tun_.stickDeadzoneDeg, tun_.stickFullscaleDeg, tun_.stickXSign,
                                tun_.stickYSign);
    out.stickX = st.x;
    out.stickY = st.y;

    // -------------------------------------------------------------- gestures
    GestureInput in;
    in.gyro = gyro;
    in.linAccel = s.accel - g * filter_.gravityMag();
    in.gravityUnit = g;
    in.pitchDeg = out.pitchDeg;
    in.rollDeg = out.rollDeg;
    in.dt = dt;
    in.nowMs = s.rxMs;
    out.swingEnded = det_.update(in, tun_, out.swing);
}
