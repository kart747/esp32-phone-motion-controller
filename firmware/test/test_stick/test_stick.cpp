#include <math.h>
#include <unity.h>

#include "config.h"
#include "stick.h"

void setUp() {}
void tearDown() {}

void test_inside_deadzone_is_zero() {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, tiltToAxis(0.0f, 5.0f, 35.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, tiltToAxis(4.9f, 5.0f, 35.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, tiltToAxis(-5.0f, 5.0f, 35.0f));
}

void test_starts_at_zero_after_deadzone() {
    // Just past the dead zone the output is tiny, not a step.
    const float v = tiltToAxis(5.3f, 5.0f, 35.0f);
    TEST_ASSERT_TRUE(v > 0.0f && v < 0.02f);
}

void test_linear_and_clamped() {
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.5f, tiltToAxis(20.0f, 5.0f, 35.0f));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, tiltToAxis(35.0f, 5.0f, 35.0f));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, tiltToAxis(90.0f, 5.0f, 35.0f));
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, tiltToAxis(-90.0f, 5.0f, 35.0f));
}

void test_sign_is_preserved() {
    TEST_ASSERT_TRUE(tiltToAxis(-20.0f, 5.0f, 35.0f) < 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -tiltToAxis(20.0f, 5.0f, 35.0f), tiltToAxis(-20.0f, 5.0f, 35.0f));
}

void test_degenerate_limits_and_nan_are_safe() {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, tiltToAxis(20.0f, 35.0f, 5.0f));   // fullscale < deadzone
    TEST_ASSERT_EQUAL_FLOAT(0.0f, tiltToAxis(NAN, 5.0f, 35.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, tiltToAxis(INFINITY, 5.0f, 35.0f));  // non-finite -> centred
}

void test_map_tilt_directions() {
    // Tilt right -> stick right (+x). Nose down -> negative y (HID up/forward).
    StickOut right = mapTilt(35.0f, 0.0f, 5.0f, 35.0f, 1.0f, 1.0f);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, right.x);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, right.y);
    StickOut noseDown = mapTilt(0.0f, -35.0f, 5.0f, 35.0f, 1.0f, 1.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, noseDown.x);
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, noseDown.y);
}

void test_map_tilt_sign_flags_invert() {
    StickOut o = mapTilt(35.0f, -35.0f, 5.0f, 35.0f, -1.0f, -1.0f);
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, o.x);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, o.y);
}

void test_axis_to_hid_range() {
    TEST_ASSERT_EQUAL_INT16(0, axisToHid(0.0f, GAMEPAD_AXIS_MAX));
    TEST_ASSERT_EQUAL_INT16(GAMEPAD_AXIS_MAX, axisToHid(1.0f, GAMEPAD_AXIS_MAX));
    TEST_ASSERT_EQUAL_INT16(-GAMEPAD_AXIS_MAX, axisToHid(-1.0f, GAMEPAD_AXIS_MAX));
    TEST_ASSERT_EQUAL_INT16(GAMEPAD_AXIS_MAX, axisToHid(7.0f, GAMEPAD_AXIS_MAX));    // clamped
    TEST_ASSERT_EQUAL_INT16(-GAMEPAD_AXIS_MAX, axisToHid(-7.0f, GAMEPAD_AXIS_MAX));
    TEST_ASSERT_EQUAL_INT16(0, axisToHid(NAN, GAMEPAD_AXIS_MAX));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_inside_deadzone_is_zero);
    RUN_TEST(test_starts_at_zero_after_deadzone);
    RUN_TEST(test_linear_and_clamped);
    RUN_TEST(test_sign_is_preserved);
    RUN_TEST(test_degenerate_limits_and_nan_are_safe);
    RUN_TEST(test_map_tilt_directions);
    RUN_TEST(test_map_tilt_sign_flags_invert);
    RUN_TEST(test_axis_to_hid_range);
    return UNITY_END();
}
