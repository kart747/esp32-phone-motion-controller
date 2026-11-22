// led_pattern - status LED state selection and blink patterns (pure logic).
//
// One LED has to say a lot, so the state is derived from the firmware's real
// condition, in priority order:
//
//   Error       highest: something failed and needs attention
//   Calibrating phone is streaming but the neutral pose is not captured yet
//   Running     phone streaming AND the BLE host is connected (fully usable)
//   Streaming   phone streaming, no BLE host
//   BleOnly     BLE host connected, no phone data
//   ApUp        idle: the access point is up, waiting
//
// A short mode flash (one blink for tennis, two for cricket) overlays the state
// for LED_MODE_FLASH_TOTAL_MS after a change. The overlay deliberately cannot hide
// Error: availability is more important than which sport is selected.
#pragma once

#include <stdint.h>

// Declared in gestures.h; forward-declared so this header stays light.
enum class Sport : uint8_t;

enum class LedState : uint8_t {
    ApUp = 0,
    BleOnly,
    Streaming,
    Running,
    Calibrating,
    Error,
};

struct LedInputs {
    bool error = false;
    bool calibrating = false;
    bool phoneStreaming = false;
    bool bleConnected = false;
};

const char *ledStateName(LedState s);

// Pure priority selection; no timing involved.
LedState ledStateFor(const LedInputs &in);

// LED level (true = lit) for the current state at time `nowMs`. Patterns are
// 16-bit masks of LED_SLOT_MS slots.
bool ledPatternLevel(LedState state, uint32_t nowMs);

// Mode indication. Returns true while the overlay owns the LED; `level` then
// says whether to light it. Error state always wins over the overlay.
bool ledModeOverlay(Sport sport, uint32_t sinceChangeMs, bool &level);