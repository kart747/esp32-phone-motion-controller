#include <math.h>
#include <string.h>

#include <string>

#include <unity.h>

#include "config.h"
#include "gestures.h"
#include "tunables.h"

namespace {

const float GRAVITY = 9.80665f;
const float DEG_TO_RAD_F = 0.01745329252f;

// Gravity direction (device frame) for a given relative roll/pitch, matching
// imu_filter.h's angle conventions.
Vec3 gravityFor(float rollDeg, float pitchDeg) {
    const float r = rollDeg * DEG_TO_RAD_F;
    const float p = pitchDeg * DEG_TO_RAD_F;
    return {-sinf(r) * cosf(p), sinf(p), cosf(r) * cosf(p)};
}

// A synthetic swing: hold still, then one half-sine pulse about `axis`, then
// settle again. Gravity is evolved with the same physics the filter uses, so
// the axes stay physically consistent.
struct Synth {
    GestureInput in;
    uint32_t tMs = 1000;
    float tsDev = 0.0f;
    Vec3 gUnit{0, 0, 1};

    explicit Synth(float rollDeg = 0, float pitchDeg = 0) { gUnit = gravityFor(rollDeg, pitchDeg); }

    void step(Vec3 gyro, float dt = 0.02f) {  // 50 Hz like the real phone
        in.gyro = gyro;
        in.up = gUnit;
        in.gravityMag = GRAVITY;
        in.accel = gUnit * GRAVITY;
        in.pitchDeg = GravityFilter_pitch(gUnit);
        in.rollDeg = GravityFilter_roll(gUnit);
        in.dt = dt;
        in.nowMs = tMs;
        tMs += static_cast<uint32_t>(dt * 1000.0f + 0.5f);
        // evolve gravity for the next sample by the same rotation
        gUnit = normalized(gUnit - cross(gyro, gUnit) * dt, gUnit);
    }

    static float GravityFilter_pitch(const Vec3 &g) {
        return atan2f(g.y, sqrtf(g.x * g.x + g.z * g.z)) * 57.29577951f;
    }
    static float GravityFilter_roll(const Vec3 &g) {
        return atan2f(-g.x, g.z) * 57.29577951f;
    }
};

struct Run {
    GestureDetector det;
    Tunables t = Tunables::defaults();
    SwingFeatures f;
    int events = 0;
    Gesture last = Gesture::None;
    const char *reason = "";
    float lastPeakGyro = 0;

    bool step(Synth &s) {
        if (det.update(s.in, t, f)) {
            events++;
            last = f.gesture;
            reason = f.rejectReason;
            lastPeakGyro = f.peakGyro;
            return true;
        }
        return false;
    }
    // Settle after a swing so the cooldown can expire.
    void settle(Synth &s, float seconds = 1.0f) {
        for (int i = 0; i < static_cast<int>(seconds / 0.02f); i++) step(s);
    }
};

// One pulse about `axis` at the given peak rate, optionally with extra linear
// acceleration along `accelDir` (added on top of gravity, as the sensor does).
// Drives the detector too, otherwise the swing would never reach it.
void pulse(Run &r, Synth &s, Vec3 axis, float peakRadS, Vec3 accelDir = {0, 0, 0},
           float peakAccel = 0.0f, float durationS = 0.25f, int steps = 13) {
    const float dt = durationS / steps;
    for (int i = 1; i <= steps; i++) {
        const float t = (i * dt) / durationS;
        const float env = sinf(3.14159265f * t);
        s.step(axis * (peakRadS * env), dt);
        s.in.accel = s.in.accel + accelDir * (peakAccel * env);
        r.step(s);
    }
}

// Still for `seconds` (also serves to fill the cooldown).
void still(Run &r, Synth &s, float seconds) {
    for (int i = 0; i < static_cast<int>(seconds / 0.02f); i++) {
        s.step({0, 0, 0});
        r.step(s);
    }
}

}  // namespace

// ------------------------------------------------------------- segmentation

void test_no_event_while_still() {
    Run r;
    Synth s;
    still(r, s, 2.0f);
    TEST_ASSERT_EQUAL_INT(0, r.events);
    TEST_ASSERT_TRUE(GestureDetector::State::Idle == r.det.state());
}

void test_swing_below_onset_produces_no_event() {
    Run r;
    Synth s;
    still(r, s, 0.5f);
    // Below SWING_ONSET_RAD_S: never starts a capture.
    for (int i = 0; i < 25; i++) {
        s.step({0, 0, SWING_ONSET_RAD_S * 0.5f});
        r.step(s);
    }
    TEST_ASSERT_EQUAL_INT(0, r.events);
}

void test_swing_above_onset_produces_exactly_one_event() {
    Run r;
    Synth s;
    still(r, s, 0.5f);
    pulse(r, s, {0, 0, 1}, 8.0f);
    still(r, s, 1.5f);
    TEST_ASSERT_EQUAL_INT(1, r.events);
    TEST_ASSERT_TRUE(r.lastPeakGyro >= 7.0f);
}

void test_cooldown_suppresses_an_immediate_second_swing() {
    Run r;
    Synth s;
    still(r, s, 0.5f);
    pulse(r, s, {0, 0, 1}, 8.0f);
    still(r, s, 0.15f);  // let the quiet hold close the capture (still in cooldown)
    TEST_ASSERT_EQUAL_INT(1, r.events);

    // Swing again immediately, inside the cooldown: swallowed entirely.
    pulse(r, s, {0, 0, 1}, 8.0f);
    still(r, s, 1.5f);
    TEST_ASSERT_EQUAL_INT(1, r.events);
}

void test_two_swings_after_cooldown_produce_two_events() {
    Run r;
    Synth s;
    still(r, s, 0.5f);
    pulse(r, s, {0, 0, 1}, 8.0f);
    still(r, s, 1.5f);  // cooldown expires, phone settles
    pulse(r, s, {0, 0, 1}, 8.0f);
    still(r, s, 1.5f);
    TEST_ASSERT_EQUAL_INT(2, r.events);
}

void test_reset_abandons_a_partial_capture() {
    Run r;
    Synth s;
    still(r, s, 0.5f);
    // Start a swing, then lose the stream mid-capture.
    for (int i = 0; i < 5; i++) {
        s.step({0, 0, 8.0f});
        r.step(s);
    }
    TEST_ASSERT_TRUE(GestureDetector::State::Capturing == r.det.state());
    r.det.reset();
    TEST_ASSERT_TRUE(GestureDetector::State::Idle == r.det.state());

    // Settling afterwards must not emit the abandoned swing.
    still(r, s, 1.0f);
    TEST_ASSERT_EQUAL_INT(0, r.events);
}

void test_long_movement_is_capped_by_max_duration() {
    Run r;
    Synth s;
    still(r, s, 0.5f);
    // Sustained rotation well past SWING_MAX_MS: the capture must end anyway.
    for (int i = 0; i < 200; i++) {
        s.step({0, 0, 8.0f});
        r.step(s);
    }
    // Exactly one event, closed off by SWING_MAX_MS rather than by silence.
    TEST_ASSERT_EQUAL_INT(1, r.events);
    TEST_ASSERT_TRUE(r.f.durationMs >= SWING_MAX_MS - 20);
    TEST_ASSERT_TRUE(r.f.durationMs <= SWING_MAX_MS + 40);
}

// ----------------------------------------------------------- classification

void test_forehand_and_backhand_by_yaw_sign() {
    Run fwd, rev;
    Synth a, b;
    still(fwd, a, 0.5f);
    pulse(fwd, a, {0, 0, 1}, 8.0f);
    still(fwd, a, 1.5f);

    still(rev, b, 0.5f);
    pulse(rev, b, {0, 0, -1}, 8.0f);
    still(rev, b, 1.5f);

    TEST_ASSERT_EQUAL_INT(1, fwd.events);
    TEST_ASSERT_EQUAL_INT(1, rev.events);
    TEST_ASSERT_EQUAL_STRING("forehand", gestureName(fwd.last));
    TEST_ASSERT_EQUAL_STRING("backhand", gestureName(rev.last));
}

void test_handedness_setting_mirrors_forehand_backhand() {
    Tunables t = Tunables::defaults();
    TEST_ASSERT_TRUE(tunablesSet(t, "yaw_sign", -1.0f));

    Run r;
    Synth s;
    r.t = t;
    still(r, s, 0.5f);
    pulse(r, s, {0, 0, 1}, 8.0f);
    still(r, s, 1.5f);
    TEST_ASSERT_EQUAL_STRING("backhand", gestureName(r.last));
}

void test_roll_dominant_swing_is_rejected() {
    Run r;
    Synth s;
    still(r, s, 0.5f);
    pulse(r, s, {0, 1, 0}, 8.0f);  // wrist roll
    still(r, s, 1.5f);
    TEST_ASSERT_EQUAL_INT(1, r.events);
    TEST_ASSERT_TRUE(r.last == Gesture::None);
    TEST_ASSERT_EQUAL_STRING("roll_dominant", r.reason);
}

void test_weak_swing_is_rejected() {
    Run r;
    Synth s;
    still(r, s, 0.5f);
    pulse(r, s, {0, 0, 1}, 3.0f);  // above onset, below TENNIS_MIN_PEAK_RAD_S
    still(r, s, 1.5f);
    TEST_ASSERT_EQUAL_INT(1, r.events);
    TEST_ASSERT_TRUE(r.last == Gesture::None);
    TEST_ASSERT_EQUAL_STRING("weak", r.reason);
}

void test_lob_needs_upward_acceleration() {
    Tunables t = Tunables::defaults();
    // A pitch-plane swing with strong upward acceleration is a lob.
    SwingFeatures f;
    const char *reason = "";
    f.peakGyro = 8.0f;
    f.onsetPitchDeg = 0.0f;
    f.intPitch = 1.2f;
    f.peakUpAccel = LOB_MIN_UP_ACCEL_MS2 + 3.0f;
    TEST_ASSERT_TRUE(Gesture::Lob == classifySwing(Sport::Tennis, f, t, &reason));

    // Same rotation without the upward component is not.
    f.peakUpAccel = 0.0f;
    TEST_ASSERT_TRUE(Gesture::None == classifySwing(Sport::Tennis, f, t, &reason));
    TEST_ASSERT_EQUAL_STRING("no_up_accel", reason);
}

void test_serve_requires_a_raised_onset_pose() {
    Tunables t = Tunables::defaults();
    SwingFeatures f;
    const char *reason = "";
    f.peakGyro = 8.0f;
    f.intPitch = -1.2f;  // chopping downward

    f.onsetPitchDeg = SERVE_ONSET_PITCH_MIN_DEG + 10.0f;
    TEST_ASSERT_TRUE(Gesture::Serve == classifySwing(Sport::Tennis, f, t, &reason));

    f.onsetPitchDeg = 0.0f;
    TEST_ASSERT_TRUE(Gesture::None == classifySwing(Sport::Tennis, f, t, &reason));
    TEST_ASSERT_EQUAL_STRING("downswing_not_overhead", reason);
}

void test_unsupported_pose_is_rejected() {
    Tunables t = Tunables::defaults();
    SwingFeatures f;
    const char *reason = "";
    f.peakGyro = 8.0f;
    f.intYaw = 2.0f;
    f.onsetPitchDeg = SWING_MAX_ONSET_TILT_DEG + 10.0f;
    TEST_ASSERT_TRUE(Gesture::None == classifySwing(Sport::Tennis, f, t, &reason));
    TEST_ASSERT_EQUAL_STRING("unsupported_pose", reason);
}

void test_ambiguous_needs_a_dominant_axis() {
    Tunables t = Tunables::defaults();
    SwingFeatures f;
    const char *reason = "";
    f.peakGyro = 8.0f;
    f.intYaw = 1.0f;
    f.intPitch = 0.95f;  // too close to yaw for SWING_DOMINANCE_RATIO
    TEST_ASSERT_TRUE(Gesture::None == classifySwing(Sport::Tennis, f, t, &reason));
    TEST_ASSERT_EQUAL_STRING("ambiguous", reason);
}

void test_small_integral_is_rejected() {
    Tunables t = Tunables::defaults();
    SwingFeatures f;
    const char *reason = "";
    f.peakGyro = 8.0f;      // energetic...
    f.intYaw = 0.1f;        // ...but barely any net rotation
    TEST_ASSERT_TRUE(Gesture::None == classifySwing(Sport::Tennis, f, t, &reason));
    TEST_ASSERT_EQUAL_STRING("small_integral", reason);
}

// --------------------------------------------------------------- tunables

void test_tunables_reject_unknown_and_out_of_range() {
    Tunables t = Tunables::defaults();
    const float before = t.onsetRadS;
    TEST_ASSERT_FALSE(tunablesSet(t, "nonsense", 1.0f));
    TEST_ASSERT_FALSE(tunablesSet(t, "onset", -5.0f));    // below range
    TEST_ASSERT_FALSE(tunablesSet(t, "onset", 500.0f));   // above range
    TEST_ASSERT_FALSE(tunablesSet(t, "onset", NAN));
    TEST_ASSERT_EQUAL_FLOAT(before, t.onsetRadS);
    TEST_ASSERT_TRUE(tunablesSet(t, "onset", 3.5f));
    TEST_ASSERT_EQUAL_FLOAT(3.5f, t.onsetRadS);
}

void test_tunables_get_round_trip() {
    Tunables t = Tunables::defaults();
    float v = 0.0f;
    TEST_ASSERT_TRUE(tunablesGet(t, "cooldown_ms", v));
    TEST_ASSERT_EQUAL_FLOAT(static_cast<float>(SWING_COOLDOWN_MS), v);
    TEST_ASSERT_FALSE(tunablesGet(t, "nope", v));
}

void test_defaults_come_from_config() {
    const Tunables t = Tunables::defaults();
    TEST_ASSERT_EQUAL_FLOAT(SWING_ONSET_RAD_S, t.onsetRadS);
    TEST_ASSERT_EQUAL_FLOAT(TENNIS_MIN_PEAK_RAD_S, t.tennisMinPeakRadS);
    TEST_ASSERT_EQUAL_FLOAT(SWING_DOMINANCE_RATIO, t.dominanceRatio);
}

void test_defaults_satisfy_whole_snapshot_validation() {
    TEST_ASSERT_TRUE(TunableError::None == validate(Tunables::defaults()));
}

// A field can be individually in range yet inconsistent with another field.
void test_cross_field_relations_are_rejected() {
    Tunables t = Tunables::defaults();
    t.endRadS = t.onsetRadS + 1.0f;          // both in range, but end > onset
    TEST_ASSERT_TRUE(TunableError::EndNotBelowOnset == validate(t));

    t = Tunables::defaults();
    t.postPeakMs = t.maxSwingMs + 10.0f;
    TEST_ASSERT_TRUE(TunableError::PostPeakExceedsMax == validate(t));

    t = Tunables::defaults();
    t.serveOnsetPitchDeg = t.maxOnsetTiltDeg + 5.0f;
    TEST_ASSERT_TRUE(TunableError::ServeAboveTilt == validate(t));
}

void test_yaw_sign_must_be_plus_or_minus_one() {
    Tunables t = Tunables::defaults();
    t.forehandYawSign = 0.0f;   // in range [-1,1] but meaningless
    TEST_ASSERT_TRUE(TunableError::YawSignNotUnit == validate(t));
    t.forehandYawSign = -1.0f;  // left-handed is fine
    TEST_ASSERT_TRUE(TunableError::None == validate(t));
}

void test_non_finite_rejected_in_snapshot() {
    Tunables t = Tunables::defaults();
    t.onsetRadS = NAN;
    TEST_ASSERT_TRUE(TunableError::NotFinite == validate(t));
}

void test_apply_validated_leaves_target_untouched_on_failure() {
    Tunables target = Tunables::defaults();
    const float before = target.onsetRadS;

    Tunables bad = Tunables::defaults();
    bad.onsetRadS = 3.0f;        // would be applied...
    bad.endRadS = 5.0f;          // ...but end > onset, so the whole set fails
    TunableError err = TunableError::None;
    TEST_ASSERT_FALSE(applyValidated(target, bad, &err));
    TEST_ASSERT_TRUE(TunableError::EndNotBelowOnset == err);
    TEST_ASSERT_EQUAL_FLOAT(before, target.onsetRadS);   // nothing half-applied

    Tunables good = Tunables::defaults();
    good.onsetRadS = 3.0f;
    TEST_ASSERT_TRUE(applyValidated(target, good, &err));
    TEST_ASSERT_EQUAL_FLOAT(3.0f, target.onsetRadS);
}

void test_set_reports_the_reason() {
    Tunables t = Tunables::defaults();
    TunableError err = TunableError::None;
    TEST_ASSERT_FALSE(tunablesSet(t, "nope", 1.0f, &err));
    TEST_ASSERT_TRUE(TunableError::UnknownKey == err);
    TEST_ASSERT_FALSE(tunablesSet(t, "onset", 1e3f, &err));
    TEST_ASSERT_TRUE(TunableError::OutOfRange == err);
    TEST_ASSERT_FALSE(tunablesSet(t, "onset", NAN, &err));
    TEST_ASSERT_TRUE(TunableError::NotFinite == err);
    TEST_ASSERT_TRUE(tunablesSet(t, "onset", 3.5f, &err));
    TEST_ASSERT_TRUE(TunableError::None == err);
}

// The dump must round-trip: small negative values keep their sign, which the
// old one-decimal fixed-point formatter lost.
static std::string g_dumpAll;
static void collectDumpLine(const char *line) {
    g_dumpAll += line;
    g_dumpAll += "\n";
}

void test_dump_round_trips_including_small_negatives() {
    Tunables t = Tunables::defaults();
    t.forehandYawSign = -1.0f;
    t.onsetRadS = 2.75f;
    t.endRadS = 0.55f;

    g_dumpAll.clear();
    tunablesDump(t, collectDumpLine);
    TEST_ASSERT_TRUE(g_dumpAll.find("yaw_sign=-1") != std::string::npos);
    TEST_ASSERT_TRUE(g_dumpAll.find("onset=2.75") != std::string::npos);
    TEST_ASSERT_TRUE(g_dumpAll.find("end=0.55") != std::string::npos);
    // Every key must appear exactly once.
    for (size_t i = 0; i < TUNABLE_FIELD_COUNT; i++) {
        const std::string key = std::string(TUNABLE_FIELDS[i].key) + "=";
        TEST_ASSERT_TRUE(g_dumpAll.find(key) != std::string::npos);
    }
}

// ------------------------------------------------------------- geometry

// Direction of the true pitch axis at a pose: the device X direction flattened
// into the horizontal plane. This is what the detector integrates against.
Vec3 pitchAxisFor(const Vec3 &up) {
    const Vec3 xh = Vec3{1.0f, 0.0f, 0.0f} - up * dot({1.0f, 0.0f, 0.0f}, up);
    return normalized(xh, {1.0f, 0.0f, 0.0f});
}

// A pose tilted in BOTH roll and pitch is where the old independent X/Y
// projections overlapped by 1/3, so a pure pitch rotation showed up as a third
// of the magnitude in roll.
void test_tilted_pitch_does_not_leak_into_roll() {
    Run r;
    Synth s(-35.26f, 30.0f);   // up ~= (0.5, 0.5, 0.707)
    still(r, s, 0.5f);
    pulse(r, s, pitchAxisFor(s.gUnit), 8.0f);
    still(r, s, 1.5f);

    TEST_ASSERT_EQUAL_INT(1, r.events);
    TEST_ASSERT_TRUE(fabsf(r.f.intPitch) > 0.5f);
    // The old basis leaked 1/3 of the pitch rotation into roll.
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.0f, r.f.intRoll);
}

void test_tilted_roll_does_not_leak_into_pitch() {
    Run r;
    Synth s(-35.26f, 30.0f);
    still(r, s, 0.5f);
    // Roll axis = up x pitch axis, i.e. orthogonal to both.
    const Vec3 rollAxis = normalized(cross(s.gUnit, pitchAxisFor(s.gUnit)),
                                     {0.0f, 1.0f, 0.0f});
    pulse(r, s, rollAxis, 8.0f);
    still(r, s, 1.5f);

    TEST_ASSERT_EQUAL_INT(1, r.events);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.0f, r.f.intPitch);
    // Roll-dominant, so it is rejected rather than treated as a stroke.
    TEST_ASSERT_TRUE(r.last == Gesture::None);
    TEST_ASSERT_EQUAL_STRING("roll_dominant", r.reason);
}

void test_degenerate_pose_is_reported_not_guessed() {
    // Phone held near vertical (gravity along device y): the swing plane is
    // undefined, so it must be reported, never confidently classified.
    Run r;
    Synth s(0.0f, 89.0f);
    still(r, s, 0.5f);
    pulse(r, s, {1, 0, 0}, 8.0f);
    still(r, s, 1.5f);

    TEST_ASSERT_EQUAL_INT(1, r.events);
    TEST_ASSERT_TRUE(r.last == Gesture::None);
    TEST_ASSERT_EQUAL_STRING("unsupported_pose", r.reason);
}

// -------------------------------------------------------------- cricket

// Cricket classes are distinguished by the same features as tennis, with
// different thresholds and labels. These are pure classifier tests: real-world
// accuracy still needs labelled cricket sessions.
void test_cricket_drive_is_a_downward_pitch_swing() {
    const Tunables t = Tunables::defaults();
    SwingFeatures f;
    const char *reason = "";
    f.peakGyro = 8.0f;
    f.intPitch = -1.2f;   // chopping down
    f.onsetPitchDeg = 0.0f;
    TEST_ASSERT_TRUE(Gesture::Drive == classifySwing(Sport::Cricket, f, t, &reason));
}

void test_cricket_pull_counts_both_sides_by_default() {
    Tunables t = Tunables::defaults();
    TEST_ASSERT_TRUE(t.pullBothSigns >= 0.5f);
    SwingFeatures f;
    const char *reason = "";
    f.peakGyro = 8.0f;

    f.intYaw = 1.2f;
    TEST_ASSERT_TRUE(Gesture::Pull == classifySwing(Sport::Cricket, f, t, &reason));
    f.intYaw = -1.2f;
    TEST_ASSERT_TRUE(Gesture::Pull == classifySwing(Sport::Cricket, f, t, &reason));
}

void test_cricket_pull_can_be_restricted_to_one_side() {
    Tunables t = Tunables::defaults();
    TEST_ASSERT_TRUE(tunablesSet(t, "pull_both", 0.0f));
    SwingFeatures f;
    const char *reason = "";
    f.peakGyro = 8.0f;

    f.intYaw = 1.2f;   // matches yaw_sign=+1
    TEST_ASSERT_TRUE(Gesture::Pull == classifySwing(Sport::Cricket, f, t, &reason));
    f.intYaw = -1.2f;  // wrong direction when restricted
    TEST_ASSERT_TRUE(Gesture::None == classifySwing(Sport::Cricket, f, t, &reason));
    TEST_ASSERT_EQUAL_STRING("pull_wrong_direction", reason);
}

void test_cricket_loft_needs_upward_acceleration() {
    const Tunables t = Tunables::defaults();
    SwingFeatures f;
    const char *reason = "";
    f.peakGyro = 8.0f;
    f.intPitch = 1.2f;
    f.peakUpAccel = CRICKET_LOFT_MIN_UP_ACCEL_MS2 + 2.0f;
    TEST_ASSERT_TRUE(Gesture::Loft == classifySwing(Sport::Cricket, f, t, &reason));

    f.peakUpAccel = 1.0f;
    TEST_ASSERT_TRUE(Gesture::None == classifySwing(Sport::Cricket, f, t, &reason));
    TEST_ASSERT_EQUAL_STRING("no_up_accel", reason);
}

void test_cricket_defensive_is_short_and_low_energy() {
    const Tunables t = Tunables::defaults();
    SwingFeatures f;
    const char *reason = "";
    f.peakGyro = CRICKET_DEFENSIVE_MAX_PEAK_RAD_S - 1.0f;
    f.durationMs = CRICKET_DEFENSIVE_MAX_MS - 20;
    f.intYaw = 0.8f;   // some real rotation, so it is a stroke not a twitch
    TEST_ASSERT_TRUE(Gesture::Defensive == classifySwing(Sport::Cricket, f, t, &reason));
}

// The defensive rule must not swallow fidgets: a twitch has no net rotation.
void test_cricket_fidget_is_not_a_defensive_block() {
    const Tunables t = Tunables::defaults();
    SwingFeatures f;
    const char *reason = "";
    f.peakGyro = CRICKET_DEFENSIVE_MAX_PEAK_RAD_S - 1.0f;
    f.durationMs = 60;
    f.intYaw = 0.05f;  // barely any rotation
    TEST_ASSERT_TRUE(Gesture::None == classifySwing(Sport::Cricket, f, t, &reason));
    TEST_ASSERT_EQUAL_STRING("small_integral", reason);
}

void test_cricket_still_rejects_weak_ambiguous_and_roll() {
    const Tunables t = Tunables::defaults();
    SwingFeatures f;
    const char *reason = "";

    f = SwingFeatures{};
    f.peakGyro = 1.0f;                       // below min_peak, too long for defensive
    f.durationMs = 400;
    f.intYaw = 1.0f;
    TEST_ASSERT_TRUE(Gesture::None == classifySwing(Sport::Cricket, f, t, &reason));
    TEST_ASSERT_EQUAL_STRING("weak", reason);

    f = SwingFeatures{};
    f.peakGyro = 8.0f;
    f.intYaw = 1.0f;
    f.intPitch = 0.95f;
    TEST_ASSERT_TRUE(Gesture::None == classifySwing(Sport::Cricket, f, t, &reason));
    TEST_ASSERT_EQUAL_STRING("ambiguous", reason);

    f = SwingFeatures{};
    f.peakGyro = 8.0f;
    f.intRoll = 1.3f;
    TEST_ASSERT_TRUE(Gesture::None == classifySwing(Sport::Cricket, f, t, &reason));
    TEST_ASSERT_EQUAL_STRING("roll_dominant", reason);
}

// The same stroke must be labelled differently per sport: a downward pitch
// swing is a tennis serve attempt or a cricket drive.
void test_sport_selects_the_label_for_the_same_stroke() {
    const Tunables t = Tunables::defaults();
    SwingFeatures f;
    const char *reason = "";
    f.peakGyro = 8.0f;
    f.intPitch = -1.2f;
    f.onsetPitchDeg = 0.0f;   // not overhead, so not a tennis serve

    TEST_ASSERT_TRUE(Gesture::None == classifySwing(Sport::Tennis, f, t, &reason));
    TEST_ASSERT_TRUE(Gesture::Drive == classifySwing(Sport::Cricket, f, t, &reason));
}

// Switching sport abandons a half-captured swing instead of scoring it under
// the wrong rules.
void test_sport_change_abandons_a_capture() {
    Run r;
    Synth s;
    still(r, s, 0.5f);
    for (int i = 0; i < 5; i++) {
        s.step({0, 0, 8.0f});
        r.step(s);
    }
    TEST_ASSERT_TRUE(GestureDetector::State::Capturing == r.det.state());

    r.det.setSport(Sport::Cricket);
    TEST_ASSERT_TRUE(GestureDetector::State::Idle == r.det.state());
    TEST_ASSERT_TRUE(Sport::Cricket == r.det.sport());

    still(r, s, 1.0f);
    TEST_ASSERT_EQUAL_INT(0, r.events);  // the abandoned swing was never scored
}

void test_cricket_detects_a_real_swing_end_to_end() {
    Run r;
    Synth s;
    r.det.setSport(Sport::Cricket);
    still(r, s, 0.5f);
    pulse(r, s, {0, 0, 1}, 8.0f);   // horizontal sweep -> pull
    still(r, s, 1.5f);
    TEST_ASSERT_EQUAL_INT(1, r.events);
    TEST_ASSERT_EQUAL_STRING("pull", gestureName(r.last));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_no_event_while_still);
    RUN_TEST(test_swing_below_onset_produces_no_event);
    RUN_TEST(test_swing_above_onset_produces_exactly_one_event);
    RUN_TEST(test_cooldown_suppresses_an_immediate_second_swing);
    RUN_TEST(test_two_swings_after_cooldown_produce_two_events);
    RUN_TEST(test_reset_abandons_a_partial_capture);
    RUN_TEST(test_long_movement_is_capped_by_max_duration);
    RUN_TEST(test_forehand_and_backhand_by_yaw_sign);
    RUN_TEST(test_handedness_setting_mirrors_forehand_backhand);
    RUN_TEST(test_roll_dominant_swing_is_rejected);
    RUN_TEST(test_weak_swing_is_rejected);
    RUN_TEST(test_lob_needs_upward_acceleration);
    RUN_TEST(test_serve_requires_a_raised_onset_pose);
    RUN_TEST(test_unsupported_pose_is_rejected);
    RUN_TEST(test_ambiguous_needs_a_dominant_axis);
    RUN_TEST(test_small_integral_is_rejected);
    RUN_TEST(test_tunables_reject_unknown_and_out_of_range);
    RUN_TEST(test_tunables_get_round_trip);
    RUN_TEST(test_defaults_come_from_config);
    RUN_TEST(test_defaults_satisfy_whole_snapshot_validation);
    RUN_TEST(test_cross_field_relations_are_rejected);
    RUN_TEST(test_yaw_sign_must_be_plus_or_minus_one);
    RUN_TEST(test_non_finite_rejected_in_snapshot);
    RUN_TEST(test_apply_validated_leaves_target_untouched_on_failure);
    RUN_TEST(test_set_reports_the_reason);
    RUN_TEST(test_dump_round_trips_including_small_negatives);
    RUN_TEST(test_tilted_pitch_does_not_leak_into_roll);
    RUN_TEST(test_tilted_roll_does_not_leak_into_pitch);
    RUN_TEST(test_degenerate_pose_is_reported_not_guessed);
    RUN_TEST(test_cricket_drive_is_a_downward_pitch_swing);
    RUN_TEST(test_cricket_pull_counts_both_sides_by_default);
    RUN_TEST(test_cricket_pull_can_be_restricted_to_one_side);
    RUN_TEST(test_cricket_loft_needs_upward_acceleration);
    RUN_TEST(test_cricket_defensive_is_short_and_low_energy);
    RUN_TEST(test_cricket_fidget_is_not_a_defensive_block);
    RUN_TEST(test_cricket_still_rejects_weak_ambiguous_and_roll);
    RUN_TEST(test_sport_selects_the_label_for_the_same_stroke);
    RUN_TEST(test_sport_change_abandons_a_capture);
    RUN_TEST(test_cricket_detects_a_real_swing_end_to_end);
    return UNITY_END();
}