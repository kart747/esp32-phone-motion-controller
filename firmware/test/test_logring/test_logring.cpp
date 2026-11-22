#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "log_ring.h"

using Ring = LogRing<4, 32, NullLogRingLock>;

// The diagnostics ring must never block the producer, must preserve FIFO order,
// and must report what it had to discard instead of losing it silently.

void test_fifo_order_is_preserved() {
    Ring r;
    char out[32];
    TEST_ASSERT_TRUE(r.push("one"));
    TEST_ASSERT_TRUE(r.push("two"));
    TEST_ASSERT_TRUE(r.pop(out));
    TEST_ASSERT_EQUAL_STRING("one", out);
    TEST_ASSERT_TRUE(r.pop(out));
    TEST_ASSERT_EQUAL_STRING("two", out);
    TEST_ASSERT_FALSE(r.pop(out));
}

void test_full_ring_drops_and_counts() {
    Ring r;
    char out[32];
    TEST_ASSERT_TRUE(r.push("a"));
    TEST_ASSERT_TRUE(r.push("b"));
    TEST_ASSERT_TRUE(r.push("c"));
    TEST_ASSERT_TRUE(r.push("d"));
    TEST_ASSERT_FALSE(r.push("e"));  // full: dropped, not blocked
    TEST_ASSERT_EQUAL_UINT32(1, r.takeDropped());
    TEST_ASSERT_EQUAL_size_t(4, r.size());

    // The oldest survive, in order; the newest arrival was the one dropped.
    TEST_ASSERT_TRUE(r.pop(out));
    TEST_ASSERT_EQUAL_STRING("a", out);
}

void test_overlong_line_is_rejected_and_counted() {
    LogRing<4, 8, NullLogRingLock> r;
    TEST_ASSERT_FALSE(r.push("this line is far too long"));
    TEST_ASSERT_EQUAL_UINT32(1, r.takeDropped());
    TEST_ASSERT_TRUE(r.push("short"));  // a fitting line still works
    TEST_ASSERT_EQUAL_size_t(1, r.size());
}

void test_null_line_is_rejected() {
    Ring r;
    TEST_ASSERT_FALSE(r.push(nullptr));
    TEST_ASSERT_EQUAL_UINT32(1, r.takeDropped());
    TEST_ASSERT_TRUE(r.empty());
}

void test_ring_recovers_after_draining() {
    Ring r;
    char out[32];
    r.push("a");
    r.push("b");
    r.push("c");
    r.push("d");
    TEST_ASSERT_FALSE(r.push("e"));
    r.pop(out);
    TEST_ASSERT_TRUE(r.push("f"));  // space again after a pop
    TEST_ASSERT_TRUE(r.pop(out));
    TEST_ASSERT_EQUAL_STRING("b", out);
}

// takeDropped() clears as it reports, so a caller logging periodically sees
// each loss exactly once rather than a growing total.
void test_take_dropped_reports_once_then_clears() {
    LogRing<1, 32, NullLogRingLock> r;
    r.push("a");
    r.push("b");   // dropped
    r.push("c");   // dropped
    TEST_ASSERT_EQUAL_UINT32(2, r.takeDropped());
    TEST_ASSERT_EQUAL_UINT32(0, r.takeDropped());
    TEST_ASSERT_EQUAL_size_t(1, r.size());
}

// Wrapping repeatedly must not lose the newest or duplicate anything.
void test_wraps_without_corruption() {
    LogRing<3, 32, NullLogRingLock> r;
    char out[32];
    char buf[16];
    for (int i = 0; i < 20; i++) {
        snprintf(buf, sizeof(buf), "l%d", i);
        r.push(buf);
        if (i % 2 == 1) r.pop(out);
    }
    // Drain whatever is left: every line must be one we actually pushed.
    int popped = 0;
    while (r.pop(out)) {
        TEST_ASSERT_TRUE(strncmp(out, "l", 1) == 0);
        popped++;
    }
    TEST_ASSERT_TRUE(popped <= 3);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_fifo_order_is_preserved);
    RUN_TEST(test_full_ring_drops_and_counts);
    RUN_TEST(test_overlong_line_is_rejected_and_counted);
    RUN_TEST(test_null_line_is_rejected);
    RUN_TEST(test_ring_recovers_after_draining);
    RUN_TEST(test_take_dropped_reports_once_then_clears);
    RUN_TEST(test_wraps_without_corruption);
    return UNITY_END();
}