// gestures - swing segmentation and tennis classification (no ML).
//
// Pipeline: IDLE --(|gyro| >= onset)--> CAPTURE --(end rule)--> classify
//           --> COOLDOWN --(time up AND quiet)--> IDLE
//
// The capture ends when the gyro has been quiet for a short hold, POST_PEAK ms
// after the strongest moment, or at the hard cap, whichever comes first.
// Features are accumulated online (no sample buffer), so memory is constant.
//
// Axes come from the calibrated up vector, not raw device axes, so the three
// rotation integrals are mutually orthogonal:
//   yaw   = gyro projected on UP            horizontal sweep (forehand/backhand)
//   pitch = horizontal part along device X  nose up / nose down (lob/serve)
//   roll  = horizontal part along device Y  wrist roll -> not a tennis stroke
// When the phone is flat that reduces to (gz, gx, gy), matching docs/axes.md.
//
// See docs/axes.md for the pose conventions and docs/testing.md for how to
// record real swings and tune the thresholds.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "imu_types.h"
#include "tunables.h"

// Which sport the strokes belong to. Independent of the output device mode:
// a recognised stroke makes the same bounded mouse click either way.
enum class Sport : uint8_t { Tennis = 0, Cricket = 1 };

const char *sportName(Sport s);

enum class Gesture : uint8_t {
    None = 0,
    // tennis
    Forehand,
    Backhand,
    Lob,
    Serve,
    // cricket
    Drive,
    Pull,
    Loft,
    Defensive,
    Count,
};

const char *gestureName(Gesture g);

struct SwingFeatures {
    uint32_t tMs = 0;            // ESP32 millis when the capture ended
    uint32_t durationMs = 0;     // onset .. end
    float    peakGyro = 0;       // rad/s, max |gyro|
    float    peakLinAccel = 0;   // m/s^2, max |a - g|
    float    peakUpAccel = 0;    // m/s^2, max (a - g) . up
    float    intYaw = 0;         // rad, about the vertical
    float    intPitch = 0;       // rad, + = nose up
    float    intRoll = 0;        // rad, + = right edge down
    float    onsetPitchDeg = 0;  // orientation when the swing began
    float    onsetRollDeg = 0;
    Gesture  gesture = Gesture::None;   // None = swing seen but not recognised
    const char *rejectReason = "";      // static string; "" when recognised
};

// Per-sample input to the detector.
struct GestureInput {
    Vec3     gyro;         // rad/s, bias removed, device frame
    Vec3     accel;        // m/s^2, raw (includes gravity)
    Vec3     up;           // unit gravity direction, device frame
    float    gravityMag = 9.80665f;
    float    pitchDeg = 0; // relative to the calibrated neutral pose
    float    rollDeg = 0;
    float    dt = 0.02f;   // s
    uint32_t nowMs = 0;
};

// Pure classification of a finished swing (exposed for tests and replay).
// Returns Gesture::None and points *reason at a static string when rejected.
Gesture classifySwing(Sport sport, const SwingFeatures &f, const Tunables &t,
                      const char **reason);

class GestureDetector {
public:
    enum class State : uint8_t { Idle, Capturing, Cooldown };

    void reset();
    State state() const { return state_; }
    Sport sport() const { return sport_; }
    // Changing sport abandons any capture in progress: a stroke cannot be
    // half-classified under one sport and scored under the other.
    void setSport(Sport s);

    // Returns true when a swing just finished (recognised or not); `out` holds
    // its features. Check out.gesture != Gesture::None for a stroke.
    bool update(const GestureInput &in, const Tunables &t, SwingFeatures &out);

private:
    void beginCapture(const GestureInput &in);

    State    state_ = State::Idle;
    Sport    sport_ = Sport::Tennis;
    uint32_t onsetMs_ = 0, peakMs_ = 0, quietSinceMs_ = 0, cooldownUntilMs_ = 0;
    bool     quiet_ = false;
    bool     axesValid_ = true;   // horizontal axes usable in this pose
    // Swing basis, fixed at onset: the stroke's plane does not move with the
    // phone, and re-deriving it per sample would drift with gyro integration
    // error. Also carries `up` so the basis stays self-consistent.
    Vec3     axisX_{1.0f, 0.0f, 0.0f};
    Vec3     axisY_{0.0f, 1.0f, 0.0f};
    Vec3     upAtOnset_{0.0f, 0.0f, 1.0f};
    SwingFeatures acc_;
};