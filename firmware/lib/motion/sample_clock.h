// sample_clock - sample interval from phone timestamps, with fallbacks.
//
// Wi-Fi jitter makes arrival time a worse source than the phone timestamp, so
// the timestamp wins when it is sane. Anything unusable (missing, repeated,
// out of order, or longer than DT_MAX_S) falls back to arrival time and then
// to the nominal rate. `discontinuity()` reports that no real delta was
// available, so time-based logic (calibration) can restart instead of
// integrating across a gap.
#pragma once

#include <stdint.h>

#include "imu_types.h"

class SampleClock {
public:
    void reset();
    // Interval in seconds for this sample. Always in (0, DT_MAX_S].
    float next(const ImuSample &s);
    // True if the last next() could not use a real delta.
    bool discontinuity() const { return discontinuity_; }

private:
    bool     havePrev_ = false;
    bool     prevHasTs_ = false;
    double   prevTs_ = 0.0;
    uint32_t prevRxMs_ = 0;
    bool     discontinuity_ = false;
};