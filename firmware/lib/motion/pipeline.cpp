#include "pipeline.h"

#include "config.h"

void OrientationPipeline::begin() {
    needCal_ = true;
    cal_.resetFailures();
    clock_.reset();
}

void OrientationPipeline::onDataLost(uint32_t nowMs) {
    (void)nowMs;
    clock_.reset();
    if (cal_.state() == Calibrator::State::Collecting) {
        // Samples either side of a stall must not share one "still" window.
        cal_.noteFailure("data_lost");
        needCal_ = true;
    }
}

void OrientationPipeline::feed(const ImuSample &s, PipelineOutput &out) {
    out = PipelineOutput{};
    const float dt = clock_.next(s);

    // ---------------------------------------------------------- calibration
    if (needCal_) {
        cal_.start(s.rxMs);
        needCal_ = false;
    } else if (cal_.state() == Calibrator::State::Collecting && clock_.discontinuity()) {
        cal_.noteFailure("gap");
        cal_.start(s.rxMs);
    }
    if (cal_.state() == Calibrator::State::Collecting) {
        const Calibrator::State st = cal_.update(s, s.rxMs);
        if (st != Calibrator::State::Done) {
            out.calibrating = true;
            out.calProgress = cal_.progress(s.rxMs);
            return;
        }
        const Calibrator::Result &r = cal_.result();
        filter_.reset(r.gravityUnit, r.gravityMag);
        rollRef_ = r.rollRefDeg;
        pitchRef_ = r.pitchRefDeg;
        out.calJustCompleted = true;
    }
    out.calibrating = false;

    // --------------------------------------------------------------- filter
    const Vec3 gyro = s.gyro - cal_.result().gyroBias;
    filter_.update(s.accel, gyro, dt, FILTER_TAU_S, ACCEL_TRUST_TOL_MS2);

    out.rollDeg = wrapDeg(filter_.rollDeg() - rollRef_);
    out.pitchDeg = wrapDeg(filter_.pitchDeg() - pitchRef_);
    out.nearSingularity = GravityFilter::nearSingularity(out.pitchDeg);
    out.anglesValid = !out.nearSingularity;
}