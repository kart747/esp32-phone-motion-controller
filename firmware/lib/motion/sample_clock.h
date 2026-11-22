// sample_clock - sample interval from phone timestamps, with fallbacks.
//
// Wi-Fi jitter makes arrival time a worse source than the phone timestamp, so
// the timestamp wins when it is sane. When a datagram carries several samples
// they share one arrival time and there is no interval to measure, so the
// nominal rate is used.
//
// `gap()` reports only that the stream really was silent for longer than
// DT_MAX_S. That distinction matters: a gap means samples either side of it
// must not be averaged into one window, whereas batching is normal traffic.
#pragma once

#include <stdint.h>

#include "imu_types.h"

class SampleClock {
public:
    void reset();
    // Interval in seconds for this sample. Always in (0, DT_MAX_S].
    float next(const ImuSample &s);
    // True if the last next() bridged a silence longer than DT_MAX_S.
    bool gap() const { return gap_; }

private:
    bool     havePrev_ = false;
    bool     prevHasTs_ = false;
    double   prevTs_ = 0.0;
    uint32_t prevRxMs_ = 0;
    bool     gap_ = false;
};