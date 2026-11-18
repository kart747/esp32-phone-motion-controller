#include <string.h>
#include <unity.h>

#include "config.h"
#include "parser.h"

namespace {

CsvLayout layout() { return CsvLayout::fromConfig(); }

size_t lenOf(const char *s) { return strlen(s); }

ParseStatus parse(const char *s) {
    ImuSample out;
    return parseCsvLine(s, lenOf(s), layout(), out);
}

PayloadResult parseText(const char *text, ImuSample *out, size_t maxOut, bool truncated = false) {
    return parsePayload(reinterpret_cast<const uint8_t *>(text), lenOf(text), layout(), truncated,
                        out, maxOut);
}

}  // namespace

void test_valid_line_parses() {
    ImuSample s;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParseStatus::Ok),
                            static_cast<uint8_t>(parseCsvLine("120,0.5,-0.25,9.81,0.1,0.2,0.3",
                                                              lenOf("120,0.5,-0.25,9.81,0.1,0.2,0.3"),
                                                              layout(), s)));
    TEST_ASSERT_EQUAL_FLOAT(0.5f, s.accel.x);
    TEST_ASSERT_EQUAL_FLOAT(9.81f, s.accel.z);
    TEST_ASSERT_EQUAL_FLOAT(0.3f, s.gyro.z);
    TEST_ASSERT_TRUE(s.hasTimestamp);
    TEST_ASSERT_EQUAL_FLOAT(0.12f, static_cast<float>(s.tsSec));  // 120 ms -> 0.12 s
}

void test_extra_columns_ignored() {
    ImuSample s;
    const char *line = "0,1,2,3,4,5,6,7,8,extra";
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParseStatus::Ok),
                            static_cast<uint8_t>(parseCsvLine(line, lenOf(line), layout(), s)));
    TEST_ASSERT_EQUAL_FLOAT(6.0f, s.gyro.z);
}

void test_missing_column_rejected() {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParseStatus::TooFewFields),
                            static_cast<uint8_t>(parse("0,1,2,3,4,5")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParseStatus::TooFewFields),
                            static_cast<uint8_t>(parse("1,2,3")));
}

void test_garbage_rejected() {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParseStatus::BadNumber),
                            static_cast<uint8_t>(parse("0,1,2,3,4,5,abc")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParseStatus::BadNumber),
                            static_cast<uint8_t>(parse("0,1,,3,4,5,6")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParseStatus::BadNumber),
                            static_cast<uint8_t>(parse("0,1,2,3,4,5,6x")));
    // "1e999" is a well-formed number that overflows to infinity -> OutOfRange.
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParseStatus::OutOfRange),
                            static_cast<uint8_t>(parse("0,1,2,3,4,5,1e999")));
}

void test_out_of_range_rejected() {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParseStatus::OutOfRange),
                            static_cast<uint8_t>(parse("0,0,0,999,0,0,0")));  // 999 m/s^2
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParseStatus::OutOfRange),
                            static_cast<uint8_t>(parse("0,0,0,9.81,0,0,500")));  // 500 rad/s
}

void test_blank_line_is_empty_not_malformed() {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParseStatus::Empty),
                            static_cast<uint8_t>(parse("")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParseStatus::Empty),
                            static_cast<uint8_t>(parse("  \t\r")));
}

void test_line_too_long_rejected() {
    char big[CSV_MAX_LINE_LEN + 8];
    memset(big, '1', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    ImuSample s;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParseStatus::LineTooLong),
                            static_cast<uint8_t>(parseCsvLine(big, strlen(big), layout(), s)));
}

void test_multiline_payload() {
    ImuSample out[CSV_MAX_SAMPLES_PER_PACKET];
    PayloadResult r = parseText("0,0,0,9.81,0,0,0\n10,0,0,9.81,0,0,0\n20,0,0,9.81,0,0,0\n", out,
                                CSV_MAX_SAMPLES_PER_PACKET);
    TEST_ASSERT_EQUAL_size_t(3, r.samples);
    TEST_ASSERT_EQUAL_size_t(0, r.malformed);
    TEST_ASSERT_FALSE(r.truncated);
    TEST_ASSERT_EQUAL_FLOAT(0.01f, static_cast<float>(out[1].tsSec));
}

void test_payload_mixes_good_and_bad() {
    ImuSample out[CSV_MAX_SAMPLES_PER_PACKET];
    PayloadResult r = parseText("0,0,0,9.81,0,0,0\nnonsense\n10,0,0,9.81,0,0,0\n", out,
                                CSV_MAX_SAMPLES_PER_PACKET);
    TEST_ASSERT_EQUAL_size_t(2, r.samples);
    TEST_ASSERT_EQUAL_size_t(1, r.malformed);
}

void test_payload_overflow_counted() {
    ImuSample out[3];
    PayloadResult r = parseText("0,0,0,9.81,0,0,0\n1,0,0,9.81,0,0,0\n2,0,0,9.81,0,0,0\n"
                                "3,0,0,9.81,0,0,0\n4,0,0,9.81,0,0,0\n",
                                out, 3);
    TEST_ASSERT_EQUAL_size_t(3, r.samples);
    TEST_ASSERT_EQUAL_size_t(2, r.overflow);
    TEST_ASSERT_EQUAL_size_t(0, r.malformed);
}

void test_truncated_datagram_not_parsed() {
    ImuSample out[CSV_MAX_SAMPLES_PER_PACKET];
    // Even a perfectly valid prefix must be discarded when the datagram was cut.
    PayloadResult r = parseText("0,0,0,9.81,0,0,0\n", out, CSV_MAX_SAMPLES_PER_PACKET, true);
    TEST_ASSERT_EQUAL_size_t(0, r.samples);
    TEST_ASSERT_TRUE(r.truncated);
}

void test_scale_conversion() {
    CsvLayout l = layout();
    l.accelScale = 9.80665f;   // app reports g instead of m/s^2
    l.gyroScale = 0.0174533f;  // app reports deg/s instead of rad/s
    ImuSample s;
    const char *line = "0,0,0,1,0,90,0";
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParseStatus::Ok),
                            static_cast<uint8_t>(parseCsvLine(line, strlen(line), l, s)));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 9.80665f, s.accel.z);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5708f, s.gyro.y);
}

void test_no_timestamp_column() {
    CsvLayout l = layout();
    l.colTimestamp = -1;
    l.colAx = 0;
    l.colAy = 1;
    l.colAz = 2;
    l.colGx = 3;
    l.colGy = 4;
    l.colGz = 5;
    ImuSample s;
    const char *line = "1,2,3,4,5,6";
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParseStatus::Ok),
                            static_cast<uint8_t>(parseCsvLine(line, strlen(line), l, s)));
    TEST_ASSERT_FALSE(s.hasTimestamp);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_valid_line_parses);
    RUN_TEST(test_extra_columns_ignored);
    RUN_TEST(test_missing_column_rejected);
    RUN_TEST(test_garbage_rejected);
    RUN_TEST(test_out_of_range_rejected);
    RUN_TEST(test_blank_line_is_empty_not_malformed);
    RUN_TEST(test_line_too_long_rejected);
    RUN_TEST(test_multiline_payload);
    RUN_TEST(test_payload_mixes_good_and_bad);
    RUN_TEST(test_payload_overflow_counted);
    RUN_TEST(test_truncated_datagram_not_parsed);
    RUN_TEST(test_scale_conversion);
    RUN_TEST(test_no_timestamp_column);
    return UNITY_END();
}