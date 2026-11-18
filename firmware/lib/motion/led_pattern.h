// led_pattern - pure logic deciding the status LED level from system state.
#pragma once

#include <stdint.h>

#include "imu_types.h"

struct LedInputs {
    bool apUp = false;
    bool phoneStreaming = false;
    bool bleConnected = false;
    bool calibrating = false;
    bool error = false;
};

enum class LedState : uint8_t { Error, Calibrating, ApUp, Streaming, BleOnly, Running };

// Priority: error > calibrating > running (phone+BLE) > phone only > BLE only > AP up.
LedState ledStateFor(const LedInputs &in);
const char *ledStateName(LedState s);

// Base pattern level at time nowMs.
bool ledPatternLevel(LedState s, uint32_t nowMs);

// Mode-change overlay: tennis = 1 flash, cricket = 2 flashes. Returns true if
// the overlay is active (sinceChangeMs short enough); `level` is its output.
bool ledModeOverlay(GameMode mode, uint32_t sinceChangeMs, bool &level);
