#include "sample_clock.h"

#include "config.h"

void SampleClock::reset() {
    havePrev_ = false;
    prevHasTs_ = false;
    prevTs_ = 0.0;
    prevRxMs_ = 0;
    gap_ = false;
}

float SampleClock::next(const ImuSample &s) {
    float dt = 1.0f / NOMINAL_SAMPLE_RATE_HZ;  // assume nominal until measured
    gap_ = false;

    if (havePrev_) {
        const bool  tsUsable = s.hasTimestamp && prevHasTs_;
        const float tsDelta = tsUsable ? static_cast<float>(s.tsSec - prevTs_) : -1.0f;
        const float arrDelta = (s.rxMs - prevRxMs_) / 1000.0f;  // unsigned: wrap-safe

        if (tsUsable && tsDelta > 0.0f && tsDelta <= DT_MAX_S) {
            dt = tsDelta;
        } else if (tsUsable && tsDelta > DT_MAX_S) {
            gap_ = true;  // the phone itself reports a long silence
        } else if (arrDelta > 0.0f && arrDelta <= DT_MAX_S) {
            dt = arrDelta;
        } else if (arrDelta > DT_MAX_S) {
            gap_ = true;
        }
        // Remaining case: arrDelta == 0, i.e. another sample from the same
        // datagram. Keep the nominal interval; that is batching, not a gap.
    }

    havePrev_ = true;
    prevHasTs_ = s.hasTimestamp;
    prevTs_ = s.tsSec;
    prevRxMs_ = s.rxMs;
    return dt;
}