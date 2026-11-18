#include <math.h>
#include <unity.h>

#include "config.h"
#include "imu_filter.h"
#include "pipeline.h"

namespace {

const float GRAVITY = 9.80665f;
const float DEG_TO_RAD_F = 0.01745329252f;

// Gravity direction (device frame) that the filter should read back as the
// given relative roll/pitch. Inverse of rollDegFrom/pitchDegFrom.
Vec3 gravityFor(float rollDeg, float pitchDeg) {
    const float r = rollDeg * DEG_TO_RAD_F;
    const float p = pitchDeg * DEG_TO_RAD_F;
    return {-sinf(r) * cosf(p), sinf(p), cosf(r) * cosf(p)};
}

// Deterministic "sensor noise" small enough to count as still.
float jitter(uint32_t i) { return (i % 2 == 0) ? 0.01f : -0.01f; }

// Driver for a stream of samples at 100 Hz.
struct Driver {
    OrientationPipeline pipe;
    PipelineOutput out;
    uint32_t tMs = 5000;
    double tsSec = 1.0;
    uint32_t n = 0;

    void feed(Vec3 gUnit, Vec3 gyro) {
        ImuSample s;
        s.accel = gUnit * GRAVITY;
        s.gyro = gyro;
        s.hasTimestamp = true;
        s.tsSec = tsSec;
        s.rxMs = tMs;
        pipe.feed(s, out);
        tMs += 10;
        tsSec += 0.01;
        n++;
    }

    // Still, flat, with the constant gyro bias the simulator injects.
    void feedStill() {
        const float j = jitter(n);
        feed(Vec3{j, j, 1.0f}, Vec3{0.02f, -0.01f, 0.015f});
    }

    bool calibrate(int maxSamples = 400) {
        for (int i = 0; i < maxSamples; i++) {
            feedStill();
            if (!out.calibrating) return true;
        }
        return false;
    }
};

}  // namespace

void test_calibration_completes_and_neutral_reads_zero() {
    Driver d;
    d.pipe.begin();
    TEST_ASSERT_TRUE(d.calibrate());
    TEST_ASSERT_FALSE(d.pipe.calibrating());

    for (int i = 0; i < 100; i++) d.feedStill();
    TEST_ASSERT_TRUE(d.out.anglesValid);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, d.out.rollDeg);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, d.out.pitchDeg);
}

void test_gyro_bias_is_measured() {
    Driver d;
    d.pipe.begin();
    TEST_ASSERT_TRUE(d.calibrate());
    const Vec3 bias = d.pipe.calibration().gyroBias;
    TEST_ASSERT_FLOAT_WITHIN(0.005f, 0.02f, bias.x);
    TEST_ASSERT_FLOAT_WITHIN(0.005f, -0.01f, bias.y);
    TEST_ASSERT_FLOAT_WITHIN(0.005f, 0.015f, bias.z);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, GRAVITY, d.pipe.calibration().gravityMag);
}

void test_calibration_rejects_movement() {
    Driver d;
    d.pipe.begin();
    for (int i = 0; i < 400; i++) {
        d.feed(Vec3{0, 0, 1}, Vec3{5.0f, 0, 0});  // far above CAL_ABORT_GYRO_RAD_S
        TEST_ASSERT_TRUE(d.out.calibrating);
    }
    TEST_ASSERT_TRUE(d.pipe.calFailures() > 0);
    TEST_ASSERT_EQUAL_STRING("moving", d.pipe.lastCalFailure());
}

void test_calibration_rejects_wrong_units() {
    Driver d;
    d.pipe.begin();
    for (int i = 0; i < 400; i++) {
        ImuSample s;
        s.accel = Vec3{0, 0, 1.0f};  // 1.0 m/s^2: looks like the app sent g
        s.gyro = Vec3{0, 0, 0};
        s.hasTimestamp = true;
        s.tsSec = d.tsSec;
        s.rxMs = d.tMs;
        d.pipe.feed(s, d.out);
        d.tMs += 10;
        d.tsSec += 0.01;
    }
    TEST_ASSERT_TRUE(d.out.calibrating);
    TEST_ASSERT_TRUE(d.pipe.calFailures() > 0);
}

void test_calibration_rejects_steep_neutral_pose() {
    Driver d;
    d.pipe.begin();
    for (int i = 0; i < 400; i++) {
        // Phone held like a racket handle: gravity along +y, far past the
        // supported flat pose and next to the Euler singularity.
        d.feed(Vec3{0, 1, 0}, Vec3{0, 0, 0});
        TEST_ASSERT_TRUE(d.out.calibrating);
    }
    TEST_ASSERT_EQUAL_STRING("neutral_not_flat", d.pipe.lastCalFailure());
}

void test_roll_and_pitch_signs() {
    Driver d;
    d.pipe.begin();
    TEST_ASSERT_TRUE(d.calibrate());

    struct Case { float roll, pitch; } cases[] = {{20, 0}, {-20, 0}, {0, 15}, {0, -15}};
    for (const Case &c : cases) {
        for (int i = 0; i < 400; i++) d.feed(gravityFor(c.roll, c.pitch), Vec3{0, 0, 0});
        TEST_ASSERT_TRUE(d.out.anglesValid);
        TEST_ASSERT_FLOAT_WITHIN(1.0f, c.roll, d.out.rollDeg);
        TEST_ASSERT_FLOAT_WITHIN(1.0f, c.pitch, d.out.pitchDeg);
    }
}

void test_roll_rotation_is_tracked() {
    Driver d;
    d.pipe.begin();
    TEST_ASSERT_TRUE(d.calibrate());
    for (int i = 0; i < 100; i++) d.feedStill();  // settle

    // Rotate about the device y axis at 30 deg/s for 0.5 s: roll should reach
    // ~+15 deg. Gravity is evolved with the same physics the filter assumes.
    const float rate = 30.0f * DEG_TO_RAD_F;  // rad/s
    const float dt = 0.01f;
    Vec3 g = gravityFor(0, 0);
    for (int i = 0; i < 50; i++) {
        g = normalized(g - cross(Vec3{0, rate, 0}, g) * dt);
        d.feed(Vec3{g.x, g.y, g.z / norm(g)}, Vec3{0, rate, 0});
    }
    TEST_ASSERT_TRUE(d.out.anglesValid);
    TEST_ASSERT_FLOAT_WITHIN(2.0f, 15.0f, d.out.rollDeg);
}

void test_near_singularity_flagged() {
    Driver d;
    d.pipe.begin();
    TEST_ASSERT_TRUE(d.calibrate());
    for (int i = 0; i < 400; i++) d.feed(gravityFor(0, 85), Vec3{0, 0, 0});
    TEST_ASSERT_TRUE(d.out.nearSingularity);
    TEST_ASSERT_FALSE(d.out.anglesValid);
}

void test_data_loss_restarts_incomplete_calibration() {
    Driver d;
    d.pipe.begin();
    for (int i = 0; i < 20; i++) d.feedStill();  // mid-window
    TEST_ASSERT_TRUE(d.out.calibrating);

    d.pipe.onDataLost(d.tMs);
    TEST_ASSERT_EQUAL_UINT32(1, d.pipe.calFailures());

    // The window restarted, so it needs the full duration again.
    d.feedStill();
    TEST_ASSERT_TRUE(d.out.calibrating);
    TEST_ASSERT_TRUE(d.calibrate());
}

void test_completed_calibration_survives_data_loss() {
    Driver d;
    d.pipe.begin();
    TEST_ASSERT_TRUE(d.calibrate());
    TEST_ASSERT_TRUE(d.pipe.calibrated());

    d.pipe.onDataLost(d.tMs);
    d.feed(gravityFor(10, 0), Vec3{0, 0, 0});
    TEST_ASSERT_FALSE(d.out.calibrating);  // no recalibration needed
    TEST_ASSERT_TRUE(d.pipe.calibrated());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_calibration_completes_and_neutral_reads_zero);
    RUN_TEST(test_gyro_bias_is_measured);
    RUN_TEST(test_calibration_rejects_movement);
    RUN_TEST(test_calibration_rejects_wrong_units);
    RUN_TEST(test_calibration_rejects_steep_neutral_pose);
    RUN_TEST(test_roll_and_pitch_signs);
    RUN_TEST(test_roll_rotation_is_tracked);
    RUN_TEST(test_near_singularity_flagged);
    RUN_TEST(test_data_loss_restarts_incomplete_calibration);
    RUN_TEST(test_completed_calibration_survives_data_loss);
    return UNITY_END();
}