// Debouncer - time-based debounce for one button (pure logic).
#pragma once

#include <stdint.h>

class Debouncer {
public:
    explicit Debouncer(uint32_t debounceMs = 30) : debounceMs_(debounceMs) {}

    // Feed the raw (already active-high) reading. Returns true if the
    // debounced state changed on this call.
    bool update(bool rawPressed, uint32_t nowMs) {
        if (rawPressed != candidate_) {
            candidate_ = rawPressed;
            changedAt_ = nowMs;
        }
        if (candidate_ != stable_ && (nowMs - changedAt_) >= debounceMs_) {
            stable_ = candidate_;
            return true;
        }
        return false;
    }
    bool pressed() const { return stable_; }

private:
    uint32_t debounceMs_;
    uint32_t changedAt_ = 0;
    bool     candidate_ = false;
    bool     stable_ = false;
};
