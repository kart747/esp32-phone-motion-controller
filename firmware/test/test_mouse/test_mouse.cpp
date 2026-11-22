#include <math.h>
#include <unity.h>

#include "config.h"
#include "gyro_mouse.h"

namespace {

MouseSettings settings() {
    return {MOUSE_GAIN_COUNTS_PER_RAD, MOUSE_RATE_DEADZONE_RAD_S, MOUSE_X_SIGN, MOUSE_Y_SIGN,
            SWING_CLICK_HOLD_MS,       MOUSE_MAX_STEP,             MOUSE_MAX_BACKLOG};
}

const Vec3 kFlatUp{0.0f, 0.0f, 1.0f};     // phone flat, screen up
const Vec3 kUprightUp{0.0f, 1.0f, 0.0f};  // phone upright, top edge up

// Feed `seconds` of constant gyro at 100 Hz starting at t0; returns the end time.
uint32_t run(GyroMouse &m, Vec3 gyro, Vec3 up, float seconds, uint32_t t0 = 0) {
    const MouseSettings s = settings();
    uint32_t t = t0;
    for (int i = 0; i < static_cast<int>(seconds * 100.0f + 0.5f); i++) {
        t += 10;
        m.feed(gyro, up, 0.01f, s);
    }
    return t;
}

// Sum everything the mouse wants to send (many take() calls).
void drainAll(GyroMouse &m, long &sx, long &sy) {
    sx = sy = 0;
    const MouseSettings s = settings();
    for (int i = 0; i < 100; i++) {
        const MouseReport r = m.take(s, 1000);
        sx += r.dx;
        sy += r.dy;
    }
}

MouseSettings speedSettings() {
    MouseSettings s = settings();
    s.speedClick = true;
    s.clickOnRadS = MOUSE_CLICK_ON_RAD_S;
    s.clickOffRadS = MOUSE_CLICK_OFF_RAD_S;
    s.clickReleaseHoldMs = MOUSE_CLICK_RELEASE_HOLD_MS;
    return s;
}

// Feed `seconds` of constant gyro at 100 Hz with the speed-click settings.
void runSpeed(GyroMouse &m, Vec3 gyro, float seconds) {
    const MouseSettings s = speedSettings();
    for (int i = 0; i < static_cast<int>(seconds * 100.0f + 0.5f); i++)
        m.feed(gyro, kFlatUp, 0.01f, s);
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_still_phone_does_not_move() {
    GyroMouse m;
    run(m, {0, 0, 0}, kFlatUp, 2.0f);
    const MouseReport r = m.take(settings(), 1000);
    TEST_ASSERT_EQUAL_INT16(0, r.dx);
    TEST_ASSERT_EQUAL_INT16(0, r.dy);
    TEST_ASSERT_FALSE(r.pressed);
}

void test_turn_left_moves_cursor_left() {
    GyroMouse m;
    run(m, {0, 0, 0.5f}, kFlatUp, 1.0f);  // counter-clockwise from above = turn left
    long sx, sy;
    drainAll(m, sx, sy);
    // (0.5 - deadzone) rad/s * gain * 1 s, with X_SIGN = -1.
    const float expected = -(0.5f - MOUSE_RATE_DEADZONE_RAD_S) * MOUSE_GAIN_COUNTS_PER_RAD;
    TEST_ASSERT_FLOAT_WITHIN(3.0f, expected, static_cast<float>(sx));
    TEST_ASSERT_EQUAL_INT32(0, sy);
}

void test_turn_right_moves_cursor_right() {
    GyroMouse m;
    run(m, {0, 0, -0.5f}, kFlatUp, 1.0f);
    long sx, sy;
    drainAll(m, sx, sy);
    const float expected = (0.5f - MOUSE_RATE_DEADZONE_RAD_S) * MOUSE_GAIN_COUNTS_PER_RAD;
    TEST_ASSERT_FLOAT_WITHIN(3.0f, expected, static_cast<float>(sx));
    TEST_ASSERT_EQUAL_INT32(0, sy);
}

void test_nose_up_moves_cursor_up() {
    GyroMouse m;
    run(m, {0.5f, 0, 0}, kFlatUp, 1.0f);  // +gyro x = nose up
    long sx, sy;
    drainAll(m, sx, sy);
    // Screen Y grows downward, so up is negative.
    const float expected = -(0.5f - MOUSE_RATE_DEADZONE_RAD_S) * MOUSE_GAIN_COUNTS_PER_RAD;
    TEST_ASSERT_FLOAT_WITHIN(3.0f, expected, static_cast<float>(sy));
    TEST_ASSERT_EQUAL_INT32(0, sx);
}

void test_turning_is_the_same_when_held_upright() {
    GyroMouse flat, upright;
    run(flat, {0, 0, 0.5f}, kFlatUp, 1.0f);
    run(upright, {0, 0.5f, 0}, kUprightUp, 1.0f);  // turning about the world vertical
    long fx, fy, ux, uy;
    drainAll(flat, fx, fy);
    drainAll(upright, ux, uy);
    TEST_ASSERT_EQUAL_INT32(fx, ux);
    TEST_ASSERT_EQUAL_INT32(0, uy);
}

void test_rotation_about_the_screen_normal_when_upright_does_not_move_x() {
    GyroMouse m;
    run(m, {0, 0, 0.5f}, kUprightUp, 1.0f);  // rolling the phone like a steering wheel
    long sx, sy;
    drainAll(m, sx, sy);
    TEST_ASSERT_EQUAL_INT32(0, sx);
    TEST_ASSERT_EQUAL_INT32(0, sy);
}

void test_deadzone_ignores_slow_drift() {
    GyroMouse m;
    const float slow = 0.5f * MOUSE_RATE_DEADZONE_RAD_S;  // inside the dead zone
    run(m, {slow, 0.0f, slow}, kFlatUp, 30.0f);
    long sx, sy;
    drainAll(m, sx, sy);
    TEST_ASSERT_EQUAL_INT32(0, sx);
    TEST_ASSERT_EQUAL_INT32(0, sy);
}

void test_slow_turns_still_accumulate() {
    // Just past the dead zone (+0.01 rad/s): each 100 Hz sample adds only a
    // fraction of a count, so this checks nothing is lost to rounding.
    GyroMouse m;
    run(m, {0, 0, MOUSE_RATE_DEADZONE_RAD_S + 0.01f}, kFlatUp, 4.0f);
    long sx, sy;
    drainAll(m, sx, sy);
    const int expected = static_cast<int>(lroundf(-0.01f * MOUSE_GAIN_COUNTS_PER_RAD * 4.0f));
    TEST_ASSERT_INT_WITHIN(1, expected, static_cast<int>(sx));
}

void test_take_limits_step_and_keeps_the_rest() {
    GyroMouse m;
    run(m, {0, 0, 2.0f}, kFlatUp, 1.0f);  // about -960 counts queued
    const MouseReport r = m.take(settings(), 1000);
    TEST_ASSERT_EQUAL_INT16(-MOUSE_MAX_STEP, r.dx);
    const MouseReport r2 = m.take(settings(), 1000);
    TEST_ASSERT_EQUAL_INT16(-MOUSE_MAX_STEP, r2.dx);  // more is still waiting
}

void test_backlog_is_capped() {
    GyroMouse m;
    run(m, {0, 0, 20.0f}, kFlatUp, 5.0f);  // would be ~49000 counts
    long sx, sy;
    drainAll(m, sx, sy);
    TEST_ASSERT_TRUE(labs(sx) <= MOUSE_MAX_BACKLOG);
}

// The click is driven by recognised swings now, not by rotation speed.
void test_fast_rotation_alone_does_not_click() {
    GyroMouse m;
    uint32_t t = run(m, {0, 0, 3.0f}, kFlatUp, 0.5f);  // much faster than the old ON threshold
    TEST_ASSERT_FALSE(m.take(settings(), t).pressed);
    TEST_ASSERT_FALSE(m.pressed());
}

void test_trigger_produces_exactly_one_bounded_pulse() {
    GyroMouse m;
    const MouseSettings s = settings();
    m.triggerClick(1000, s);
    TEST_ASSERT_TRUE(m.take(s, 1000).pressed);            // press is visible at once
    TEST_ASSERT_TRUE(m.take(s, 1000 + SWING_CLICK_HOLD_MS - 1).pressed);
    TEST_ASSERT_FALSE(m.take(s, 1000 + SWING_CLICK_HOLD_MS).pressed);  // released on time
    TEST_ASSERT_FALSE(m.pressed());
}

void test_trigger_during_a_pulse_is_ignored_and_counted() {
    GyroMouse m;
    const MouseSettings s = settings();
    m.triggerClick(1000, s);
    m.triggerClick(1020, s);  // still inside the pulse
    m.triggerClick(1040, s);
    TEST_ASSERT_EQUAL_UINT32(2, m.ignoredTriggers());
    // One pulse only: it ends at the FIRST trigger's deadline, and no second
    // click is replayed afterwards.
    TEST_ASSERT_FALSE(m.take(s, 1000 + SWING_CLICK_HOLD_MS).pressed);
    for (uint32_t t = 1000 + SWING_CLICK_HOLD_MS; t < 1400; t += 8) {
        TEST_ASSERT_FALSE(m.take(s, t).pressed);
    }
}

void test_trigger_after_release_starts_a_new_pulse() {
    GyroMouse m;
    const MouseSettings s = settings();
    m.triggerClick(1000, s);
    TEST_ASSERT_FALSE(m.take(s, 1000 + SWING_CLICK_HOLD_MS).pressed);
    m.triggerClick(2000, s);
    TEST_ASSERT_TRUE(m.take(s, 2000).pressed);
    TEST_ASSERT_FALSE(m.take(s, 2000 + SWING_CLICK_HOLD_MS).pressed);
}

void test_clear_click_releases_immediately() {
    GyroMouse m;
    const MouseSettings s = settings();
    m.triggerClick(1000, s);
    TEST_ASSERT_TRUE(m.pressed());
    m.clearClick();                       // safety release
    TEST_ASSERT_FALSE(m.take(s, 1001).pressed);
}

void test_pulse_survives_millis_wrap() {
    GyroMouse m;
    const MouseSettings s = settings();
    const uint32_t start = 0xFFFFFFF0u;
    m.triggerClick(start, s);
    TEST_ASSERT_TRUE(m.take(s, start + 10).pressed);       // wrapped value
    TEST_ASSERT_FALSE(m.take(s, start + SWING_CLICK_HOLD_MS).pressed);
}

void test_zero_hold_disables_clicks() {
    GyroMouse m;
    MouseSettings s = settings();
    s.clickHoldMs = 0;
    m.triggerClick(1000, s);
    TEST_ASSERT_FALSE(m.take(s, 1000).pressed);
}

void test_reset_clears_movement_and_click() {
    GyroMouse m;
    run(m, {0, 0, 2.0f}, kFlatUp, 0.5f);
    m.reset();
    const MouseReport r = m.take(settings(), 1000);
    TEST_ASSERT_EQUAL_INT16(0, r.dx);
    TEST_ASSERT_EQUAL_INT16(0, r.dy);
    TEST_ASSERT_FALSE(r.pressed);
}

void test_bad_input_is_ignored() {
    GyroMouse m;
    const MouseSettings s = settings();
    m.feed({NAN, 0, 0}, kFlatUp, 0.01f, s);
    m.feed({0, 0, 5.0f}, kFlatUp, 0.0f, s);   // dt = 0
    m.feed({0, 0, 5.0f}, kFlatUp, -0.01f, s); // negative dt
    const MouseReport r = m.take(s, 1000);
    TEST_ASSERT_EQUAL_INT16(0, r.dx);
    TEST_ASSERT_FALSE(r.pressed);
}

void test_speed_click_holds_while_turning_fast() {
    GyroMouse m;
    runSpeed(m, {0, 0, 0.5f}, 1.0f);               // slow turn: no click
    TEST_ASSERT_FALSE(m.take(speedSettings(), 0).pressed);
    runSpeed(m, {0, 0, 3.0f}, 0.1f);               // fast slice: held
    TEST_ASSERT_TRUE(m.take(speedSettings(), 0).pressed);
    runSpeed(m, {0, 0, 1.0f}, 0.5f);               // between OFF and ON: still held
    TEST_ASSERT_TRUE(m.pressed());
}

void test_speed_click_releases_after_the_hold() {
    GyroMouse m;
    runSpeed(m, {0, 0, 3.0f}, 0.1f);
    runSpeed(m, {0, 0, 0.2f}, 0.05f);              // slow, shorter than the hold
    TEST_ASSERT_TRUE(m.pressed());
    runSpeed(m, {0, 0, 0.2f}, 0.1f);               // past the hold: released
    TEST_ASSERT_FALSE(m.take(speedSettings(), 0).pressed);
}

void test_speed_click_fast_again_restarts_the_hold() {
    GyroMouse m;
    runSpeed(m, {0, 0, 3.0f}, 0.1f);
    runSpeed(m, {0, 0, 0.2f}, 0.05f);
    runSpeed(m, {0, 0, 3.0f}, 0.05f);
    runSpeed(m, {0, 0, 0.2f}, 0.05f);
    TEST_ASSERT_TRUE(m.pressed());
}

void test_speed_click_off_when_disabled() {
    GyroMouse m;
    run(m, {0, 0, 3.0f}, kFlatUp, 0.5f);           // default settings: speedClick = false
    TEST_ASSERT_FALSE(m.take(settings(), 0).pressed);
}

void test_speed_click_cleared_by_safety_release_and_reset() {
    GyroMouse m;
    runSpeed(m, {0, 0, 3.0f}, 0.1f);
    m.clearClick();
    TEST_ASSERT_FALSE(m.pressed());
    runSpeed(m, {0, 0, 3.0f}, 0.1f);
    m.reset();
    TEST_ASSERT_FALSE(m.take(speedSettings(), 0).pressed);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_still_phone_does_not_move);
    RUN_TEST(test_turn_left_moves_cursor_left);
    RUN_TEST(test_turn_right_moves_cursor_right);
    RUN_TEST(test_nose_up_moves_cursor_up);
    RUN_TEST(test_turning_is_the_same_when_held_upright);
    RUN_TEST(test_rotation_about_the_screen_normal_when_upright_does_not_move_x);
    RUN_TEST(test_deadzone_ignores_slow_drift);
    RUN_TEST(test_slow_turns_still_accumulate);
    RUN_TEST(test_take_limits_step_and_keeps_the_rest);
    RUN_TEST(test_backlog_is_capped);
    RUN_TEST(test_fast_rotation_alone_does_not_click);
    RUN_TEST(test_trigger_produces_exactly_one_bounded_pulse);
    RUN_TEST(test_trigger_during_a_pulse_is_ignored_and_counted);
    RUN_TEST(test_trigger_after_release_starts_a_new_pulse);
    RUN_TEST(test_clear_click_releases_immediately);
    RUN_TEST(test_pulse_survives_millis_wrap);
    RUN_TEST(test_zero_hold_disables_clicks);
    RUN_TEST(test_reset_clears_movement_and_click);
    RUN_TEST(test_bad_input_is_ignored);
    RUN_TEST(test_speed_click_holds_while_turning_fast);
    RUN_TEST(test_speed_click_releases_after_the_hold);
    RUN_TEST(test_speed_click_fast_again_restarts_the_hold);
    RUN_TEST(test_speed_click_off_when_disabled);
    RUN_TEST(test_speed_click_cleared_by_safety_release_and_reset);
    return UNITY_END();
}
