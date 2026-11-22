#include <unity.h>

#include "config.h"
#include "output_gate.h"
#include "packet_stats.h"
#include "sample_clock.h"

void test_watchdog_ignores_malformed_traffic() {
    PacketStats s;
    s.reset(1000);
    TEST_ASSERT_TRUE(s.isStale(1000, DATA_TIMEOUT_MS));  // nothing yet

    // Lots of datagrams that never yield a sample must not look alive.
    for (uint32_t t = 1000; t < 3000; t += 10) {
        s.onDatagram(t);
        s.onParsed(0, 1, 0, false);
    }
    TEST_ASSERT_TRUE(s.isStale(3000, DATA_TIMEOUT_MS));
    TEST_ASSERT_EQUAL_UINT32(200, s.datagrams());
    TEST_ASSERT_EQUAL_UINT32(200, s.malformed());
    TEST_ASSERT_EQUAL_UINT32(0, s.samples());
    TEST_ASSERT_TRUE(s.msSinceLastValid(3000) == UINT32_MAX);
}

void test_watchdog_tracks_last_valid_sample() {
    PacketStats s;
    s.reset(0);
    s.onValidSample(1000);
    TEST_ASSERT_FALSE(s.isStale(1499, DATA_TIMEOUT_MS));  // 499 ms
    TEST_ASSERT_TRUE(s.isStale(1501, DATA_TIMEOUT_MS));   // 501 ms
    TEST_ASSERT_EQUAL_UINT32(500, s.msSinceLastValid(1500));

    s.onValidSample(1600);  // recovery
    TEST_ASSERT_FALSE(s.isStale(1600, DATA_TIMEOUT_MS));
}

void test_watchdog_survives_millis_wrap() {
    PacketStats s;
    s.reset(0xFFFFFF00u);
    s.onValidSample(0xFFFFFFF0u);
    // 100 ms later the counter has wrapped.
    TEST_ASSERT_FALSE(s.isStale(0x00000054u, DATA_TIMEOUT_MS));
    // 501 ms later (0xFFFFFFF0 + 501 wraps to 0x1F5) it must be stale.
    TEST_ASSERT_TRUE(s.isStale(0x000001F5u, DATA_TIMEOUT_MS));
}

void test_rate_window() {
    PacketStats s;
    s.reset(0);
    for (uint32_t i = 0; i < 100; i++) {
        s.onDatagram(i * 10);
        s.onParsed(1, 0, 0, false);
        s.onValidSample(i * 10);
    }
    s.tick(1000);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 100.0f, s.packetsPerSec());
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 100.0f, s.samplesPerSec());
}

void test_gate_is_idempotent() {
    OutputGate g;
    TEST_ASSERT_TRUE(g.update(false) == GateAction::None);  // baseline
    TEST_ASSERT_TRUE(g.update(false) == GateAction::None);
    TEST_ASSERT_TRUE(g.update(true) == GateAction::Resume);
    TEST_ASSERT_TRUE(g.update(true) == GateAction::None);   // not repeated
    TEST_ASSERT_TRUE(g.update(false) == GateAction::Release);
    TEST_ASSERT_TRUE(g.update(false) == GateAction::None);  // release once per transition
    TEST_ASSERT_FALSE(g.live());
}

void test_gate_starts_live_without_release() {
    OutputGate g;
    TEST_ASSERT_TRUE(g.update(true) == GateAction::None);
    TEST_ASSERT_TRUE(g.live());
}

void test_clock_prefers_timestamps_and_rejects_gaps() {
    SampleClock c;
    c.reset();
    ImuSample a;
    a.hasTimestamp = true;
    a.tsSec = 1.000;
    a.rxMs = 5000;
    c.next(a);  // first sample has no previous interval to measure
    TEST_ASSERT_FALSE(c.gap());

    ImuSample b;
    b.hasTimestamp = true;
    b.tsSec = 1.010;
    b.rxMs = 9183;  // arrival time is jittery, timestamp is not
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.010f, c.next(b));
    TEST_ASSERT_FALSE(c.gap());

    ImuSample gap;
    gap.hasTimestamp = true;
    gap.tsSec = 2.000;  // 990 ms gap
    gap.rxMs = 10000;
    const float dt = c.next(gap);
    TEST_ASSERT_TRUE(c.gap());
    TEST_ASSERT_TRUE(dt > 0.0f && dt <= DT_MAX_S);
}

void test_clock_handles_out_of_order_timestamps() {
    SampleClock c;
    c.reset();
    ImuSample a;
    a.hasTimestamp = true;
    a.tsSec = 5.000;
    a.rxMs = 1000;
    c.next(a);

    ImuSample b;
    b.hasTimestamp = true;
    b.tsSec = 4.990;  // went backwards: fall back to arrival time
    b.rxMs = 1010;
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.010f, c.next(b));
    TEST_ASSERT_FALSE(c.gap());
}

void test_clock_without_timestamps_uses_arrival() {
    SampleClock c;
    c.reset();
    ImuSample a;
    a.hasTimestamp = false;
    a.rxMs = 2000;
    c.next(a);
    ImuSample b;
    b.hasTimestamp = false;
    b.rxMs = 2020;
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.020f, c.next(b));
    TEST_ASSERT_FALSE(c.gap());
}

// Several samples travelling in one datagram share an arrival time. That is
// normal batching, not a data gap, so it must not be reported as one.
void test_clock_treats_batched_samples_as_normal() {
    SampleClock c;
    c.reset();
    for (int k = 0; k < 4; k++) {
        ImuSample s;
        s.hasTimestamp = false;
        s.rxMs = 5000;  // same datagram
        const float dt = c.next(s);
        TEST_ASSERT_FALSE(c.gap());
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f / NOMINAL_SAMPLE_RATE_HZ, dt);
    }
    ImuSample next;
    next.hasTimestamp = false;
    next.rxMs = 5040;  // next datagram, 40 ms later
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.040f, c.next(next));
    TEST_ASSERT_FALSE(c.gap());
}

// 50 Hz (20 ms) is the rate the real phone runs at: it must never look like a gap.
void test_clock_accepts_real_phone_rate() {
    SampleClock c;
    c.reset();
    ImuSample s;
    s.hasTimestamp = false;
    s.rxMs = 1000;
    c.next(s);
    for (int i = 0; i < 10; i++) {
        s.rxMs += 20;
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.020f, c.next(s));
        TEST_ASSERT_FALSE(c.gap());
    }
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_watchdog_ignores_malformed_traffic);
    RUN_TEST(test_watchdog_tracks_last_valid_sample);
    RUN_TEST(test_watchdog_survives_millis_wrap);
    RUN_TEST(test_rate_window);
    RUN_TEST(test_gate_is_idempotent);
    RUN_TEST(test_gate_starts_live_without_release);
    RUN_TEST(test_clock_prefers_timestamps_and_rejects_gaps);
    RUN_TEST(test_clock_handles_out_of_order_timestamps);
    RUN_TEST(test_clock_without_timestamps_uses_arrival);
    RUN_TEST(test_clock_treats_batched_samples_as_normal);
    RUN_TEST(test_clock_accepts_real_phone_rate);
    return UNITY_END();
}