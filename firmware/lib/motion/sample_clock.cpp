#include "sample_clock.h"

#include "config.h"

void SampleClock::reset() {
    havePrev_ = false;
    prevHasTs_ = false;
    prevTs_ = 0.0;
    prevRxMs_ = 0;
    discontinuity_ = false;
}

float SampleClock::next(const ImuSample &s) {
    float dt = 1.0f / NOMINAL_SAMPLE_RATE_HZ;
    bool usedRealDelta = false;

    if (havePrev_) {
        float candidate = -1.0f;
        if (s.hasTimestamp && prevHasTs_) {
            candidate = static_cast<float>(s.tsSec - prevTs_);
        }
        if (!(candidate > 0.0f && candidate <= DT_MAX_S)) {
            candidate = (s.rxMs - prevRxMs_) / 1000.0f;  // unsigned: wrap-safe
        }
        if (candidate > 0.0f && candidate <= DT_MAX_S) {
            dt = candidate;
            usedRealDelta = true;
        }
    }

    havePrev_ = true;
    prevHasTs_ = s.hasTimestamp;
    prevTs_ = s.tsSec;
    prevRxMs_ = s.rxMs;
    discontinuity_ = !usedRealDelta;
    return dt;
}