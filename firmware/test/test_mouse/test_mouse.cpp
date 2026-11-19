#include <math.h>
#include <unity.h>

#include "config.h"
#include "gyro_mouse.h"

namespace {

MouseSettings settings() {
    return {MOUSE_GAIN_COUNTS_PER_RAD, MOUSE_RATE_DEADZONE_RAD_S, MOUSE_X_SIGN, MOUSE_Y_SIGN,
            MOUSE_CLICK_ON_RAD_S,      MOUSE_CLICK_OFF_RAD_S,     MOUSE_CLICK_RELEASE_HOLD_MS,
            MOUSE_MAX_STEP,            MOUSE_MAX_BACKLOG};
}

const Vec3 kFlatUp{0.0f, 0.0f, 1.0f};     // phone flat, screen up
const Vec3 kUprightUp{0.0f, 1.0f, 0.0f};  // phone upright, top edge up

// Feed `seconds` of constant gyro at 100 Hz starting at t0; returns the end time.
uint32_t run(GyroMouse &m, Vec3 gyro, Vec3 up, float seconds, uint32_t t0 = 0) {
    const MouseSettings s = settings();
    uint32_t t = t0;
    for (int i = 0; i < static_cast<int>(seconds * 100.0f + 0.5f); i++) {
        t += 10;
        m.feed(gyro, up, 0.01f, t, s);
    }
    return t;
}

// Sum everything the mouse wants to send (many take() calls).
void drainAll(GyroMouse &m, long &sx, long &sy) {
    sx = sy = 0;
    const MouseSettings s = settings();
    for (int i = 0; i < 100; i++) {
        const MouseReport r = m.take(s);
        sx += r.dx;
        sy += r.dy;
    }
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_still_phone_does_not_move() {
    GyroMouse m;
    run(m, {0, 0, 0}, kFlatUp, 2.0f);
    const MouseReport r = m.take(settings());
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
    TEST_ASSERT_TRUE(sx > 100);
    TEST_ASSERT_EQUAL_INT32(0, sy);
}

void test_nose_up_moves_cursor_up() {
    GyroMouse m;
    run(m, {0.5f, 0, 0}, kFlatUp, 1.0f);  // +gyro x = nose up
    long sx, sy;
    drainAll(m, sx, sy);
    TEST_ASSERT_TRUE(sy < -100);          // screen Y grows downward, so up is negative
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
    run(m, {0.05f, 0.0f, 0.05f}, kFlatUp, 30.0f);
    long sx, sy;
    drainAll(m, sx, sy);
    TEST_ASSERT_EQUAL_INT32(0, sx);
    TEST_ASSERT_EQUAL_INT32(0, sy);
}

void test_slow_turns_still_accumulate() {
    // 0.09 rad/s is just past the dead zone: 0.01 * 500 = 5 counts/s. At 100 Hz
    // each sample adds 0.05 counts, so nothing is lost to rounding.
    GyroMouse m;
    run(m, {0, 0, 0.09f}, kFlatUp, 4.0f);
    long sx, sy;
    drainAll(m, sx, sy);
    TEST_ASSERT_INT_WITHIN(1, -20, static_cast<int>(sx));
}

void test_take_limits_step_and_keeps_the_rest() {
    GyroMouse m;
    run(m, {0, 0, 2.0f}, kFlatUp, 1.0f);  // about -960 counts queued
    const MouseReport r = m.take(settings());
    TEST_ASSERT_EQUAL_INT16(-MOUSE_MAX_STEP, r.dx);
    const MouseReport r2 = m.take(settings());
    TEST_ASSERT_EQUAL_INT16(-MOUSE_MAX_STEP, r2.dx);  // more is still waiting
}

void test_backlog_is_capped() {
    GyroMouse m;
    run(m, {0, 0, 20.0f}, kFlatUp, 5.0f);  // would be ~49000 counts
    long sx, sy;
    drainAll(m, sx, sy);
    TEST_ASSERT_TRUE(labs(sx) <= MOUSE_MAX_BACKLOG);
}

void test_click_presses_when_fast_and_has_hysteresis() {
    GyroMouse m;
    uint32_t t = run(m, {0, 0, 2.0f}, kFlatUp, 0.1f);
    TEST_ASSERT_TRUE(m.take(settings()).pressed);
    // Between OFF and ON: stays pressed.
    t = run(m, {0, 0, 1.0f}, kFlatUp, 0.5f, t);
    TEST_ASSERT_TRUE(m.pressed());
}

void test_click_releases_only_after_the_hold_time() {
    GyroMouse m;
    uint32_t t = run(m, {0, 0, 2.0f}, kFlatUp, 0.1f);
    // Slow for less than the hold time: still pressed.
    t = run(m, {0, 0, 0.3f}, kFlatUp, 0.05f, t);
    TEST_ASSERT_TRUE(m.pressed());
    // Slow past the hold time: released.
    t = run(m, {0, 0, 0.3f}, kFlatUp, 0.2f, t);
    TEST_ASSERT_FALSE(m.pressed());
}

void test_fast_again_restarts_the_release_hold() {
    GyroMouse m;
    uint32_t t = run(m, {0, 0, 2.0f}, kFlatUp, 0.1f);
    t = run(m, {0, 0, 0.3f}, kFlatUp, 0.05f, t);   // starts the hold
    t = run(m, {0, 0, 2.0f}, kFlatUp, 0.05f, t);   // fast again: restart
    t = run(m, {0, 0, 0.3f}, kFlatUp, 0.05f, t);   // short slow period again
    TEST_ASSERT_TRUE(m.pressed());
}

void test_reset_clears_movement_and_click() {
    GyroMouse m;
    run(m, {0, 0, 2.0f}, kFlatUp, 0.5f);
    m.reset();
    const MouseReport r = m.take(settings());
    TEST_ASSERT_EQUAL_INT16(0, r.dx);
    TEST_ASSERT_EQUAL_INT16(0, r.dy);
    TEST_ASSERT_FALSE(r.pressed);
}

void test_bad_input_is_ignored() {
    GyroMouse m;
    const MouseSettings s = settings();
    m.feed({NAN, 0, 0}, kFlatUp, 0.01f, 10, s);
    m.feed({0, 0, 5.0f}, kFlatUp, 0.0f, 20, s);   // dt = 0
    m.feed({0, 0, 5.0f}, kFlatUp, -0.01f, 30, s); // negative dt
    const MouseReport r = m.take(s);
    TEST_ASSERT_EQUAL_INT16(0, r.dx);
    TEST_ASSERT_FALSE(r.pressed);
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
    RUN_TEST(test_click_presses_when_fast_and_has_hysteresis);
    RUN_TEST(test_click_releases_only_after_the_hold_time);
    RUN_TEST(test_fast_again_restarts_the_release_hold);
    RUN_TEST(test_reset_clears_movement_and_click);
    RUN_TEST(test_bad_input_is_ignored);
    return UNITY_END();
}
