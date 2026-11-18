#include "led_pattern.h"

#include "config.h"

LedState ledStateFor(const LedInputs &in) {
    if (in.error) return LedState::Error;
    if (in.calibrating) return LedState::Calibrating;
    if (in.phoneStreaming && in.bleConnected) return LedState::Running;
    if (in.phoneStreaming) return LedState::Streaming;
    if (in.bleConnected) return LedState::BleOnly;
    return LedState::ApUp;
}

const char *ledStateName(LedState s) {
    switch (s) {
        case LedState::Error:       return "error";
        case LedState::Calibrating: return "calibrating";
        case LedState::ApUp:        return "ap_up";
        case LedState::Streaming:   return "phone_streaming";
        case LedState::BleOnly:     return "ble_connected_no_phone";
        case LedState::Running:     return "running";
    }
    return "?";
}

bool ledPatternLevel(LedState s, uint32_t nowMs) {
    uint16_t pat = 0;
    switch (s) {
        case LedState::Error:       pat = LED_PAT_ERROR; break;
        case LedState::Calibrating: pat = LED_PAT_CALIBRATING; break;
        case LedState::ApUp:        pat = LED_PAT_AP_UP; break;
        case LedState::Streaming:   pat = LED_PAT_STREAMING; break;
        case LedState::BleOnly:     pat = LED_PAT_BLE_ONLY; break;
        case LedState::Running:     pat = LED_PAT_RUNNING; break;
    }
    const uint32_t slot = (nowMs / LED_SLOT_MS) % 16;
    return (pat >> slot) & 1u;
}

bool ledModeOverlay(GameMode mode, uint32_t sinceChangeMs, bool &level) {
    if (sinceChangeMs >= LED_MODE_FLASH_TOTAL_MS) return false;
    const uint32_t flashes = mode == GameMode::Tennis ? 1 : 2;
    const uint32_t idx = sinceChangeMs / LED_MODE_FLASH_PERIOD_MS;
    const uint32_t phase = sinceChangeMs % LED_MODE_FLASH_PERIOD_MS;
    level = idx < flashes && phase < LED_MODE_FLASH_ON_MS;
    return true;  // overlay owns the LED for its whole duration (off between flashes)
}
