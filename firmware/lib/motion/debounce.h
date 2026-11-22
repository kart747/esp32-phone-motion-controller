// debounce - time-based debounce for one active-low button (pure logic).
//
// Wired to GND with the internal pull-up, so a pressed button reads LOW. The
// caller converts to "pressed" before feeding this, keeping the logic testable.
//
// A button held down at boot must NOT count as a press: `update()` only reports
// a change once it has observed a stable edge after the first call, so the
// initial state is adopted silently.
#pragma once

#include <stdint.h>

class Debouncer {
public:
    explicit Debouncer(uint32_t debounceMs = 30) : debounceMs_(debounceMs) {}

    // Feed the raw (already active-high) reading. Returns true if the debounced
    // state changed on this call.
    bool update(bool rawPressed, uint32_t nowMs) {
        if (!seeded_) {
            // First observation: adopt it as the stable state without firing.
            seeded_ = true;
            candidate_ = rawPressed;
            stable_ = rawPressed;
            changedAt_ = nowMs;
            return false;
        }
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
    bool     seeded_ = false;
};