// output_gate - edge detection between "live" and "safe" output.
//
// Phase 2 has no gamepad yet, but the safety rule (no valid data for
// DATA_TIMEOUT_MS -> release all inputs) has to exist and be testable now.
// The gate is idempotent: repeated calls in the same state report nothing to
// do, so the caller can drive "release" exactly once per transition. Phase 4
// plugs the BLE HID adapter into these actions.
#pragma once

#include <stdint.h>

enum class GateAction : uint8_t {
    None = 0,
    Release,  // just lost the stream (or was never live): centre/release everything
    Resume,   // just got a usable stream back
};

class OutputGate {
public:
    // Feed the current liveness each wake. The first call only records the
    // baseline: nothing was driven yet, so there is nothing to release.
    GateAction update(bool live);

    bool live() const { return live_; }
    bool announced() const { return announced_; }

private:
    bool live_ = false;
    bool announced_ = false;
};

inline GateAction OutputGate::update(bool live) {
    if (!announced_) {
        announced_ = true;
        live_ = live;
        return GateAction::None;
    }
    if (live == live_) return GateAction::None;
    live_ = live;
    return live_ ? GateAction::Resume : GateAction::Release;
}