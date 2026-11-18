// gestures - swing segmentation, feature extraction and rule-based
// classification (no ML). See docs/axes.md for the axis conventions.
//
// Pipeline: IDLE --(gyro magnitude >= onset)--> CAPTURE --(end rule)--> classify
//           --> COOLDOWN --(time up AND phone quiet)--> IDLE
//
// The capture window is "onset .. ~peak + post-peak": it ends when the
// gyro has been quiet for a short hold, or POST_PEAK ms after the strongest
// moment, or at the hard cap, whichever comes first. Features are accumulated
// online (no sample buffer), so memory use is constant.
#pragma once

#include <stdint.h>

#include "imu_types.h"
#include "tunables.h"

struct SwingFeatures {
    uint32_t tMs = 0;            // time the swing ended (ESP32 millis)
    uint32_t durationMs = 0;     // onset -> end of capture
    float peakGyro = 0;          // rad/s, max |omega|
    float peakLinAccel = 0;      // m/s^2, max |a - g| (gravity removed)
    float peakUpAccel = 0;       // m/s^2, max (a - g) . up  ("upward component")
    float intYaw = 0;            // rad, integral of omega . up  (+ = counter-clockwise from above)
    float intPitch = 0;          // rad, integral of gyro x      (+ = nose up)
    float intRoll = 0;           // rad, integral of gyro y      (+ = right edge down)
    float onsetPitchDeg = 0;     // pitch relative to neutral when the swing began
    float onsetRollDeg = 0;
    Gesture gesture = Gesture::None;       // None = swing seen but not recognised
    const char *rejectReason = "";         // why it was not recognised (static string)
};

// Per-sample input to the detector.
struct GestureInput {
    Vec3     gyro;            // rad/s, bias removed
    Vec3     linAccel;        // m/s^2, gravity removed
    Vec3     gravityUnit;     // up, device frame
    float    pitchDeg = 0;    // relative to the neutral pose
    float    rollDeg = 0;
    float    dt = 0.01f;      // s
    uint32_t nowMs = 0;
};

// Pure classification of a finished swing (exposed for unit tests and
// offline experiments). Returns Gesture::None and sets *reason if rejected.
Gesture classifySwing(GameMode mode, const SwingFeatures &f, const Tunables &t, const char **reason);

class GestureDetector {
public:
    enum class State : uint8_t { Idle, Capturing, Cooldown };

    void reset();
    void setMode(GameMode m) { mode_ = m; reset(); }
    GameMode mode() const { return mode_; }
    State state() const { return state_; }

    // Returns true when a swing just finished (recognised or not); `out` holds
    // its features. Check out.gesture != None for a recognised gesture.
    bool update(const GestureInput &in, const Tunables &t, SwingFeatures &out);

private:
    void beginCapture(const GestureInput &in);

    GameMode mode_ = GameMode::Tennis;
    State    state_ = State::Idle;
    // capture accumulators
    uint32_t onsetMs_ = 0, peakMs_ = 0, quietSinceMs_ = 0;
    bool     quiet_ = false;
    SwingFeatures acc_;
    // cooldown
    uint32_t cooldownUntilMs_ = 0;
};
