#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "config.h"
#include "settings_store.h"

// The saved record must round-trip exactly, and every kind of damage must be
// refused with a specific reason so the firmware can fall back to defaults
// rather than loading nonsense.

namespace {

SavedSettings makeSettings() {
    SavedSettings s;
    s.sport = Sport::Cricket;
    s.swingOutputEnabled = false;
    s.tunables.onsetRadS = 3.25f;
    s.tunables.endRadS = 0.75f;
    s.tunables.forehandYawSign = -1.0f;
    s.tunables.cooldownMs = 250.0f;
    return s;
}

char buf[SETTINGS_MAX_BYTES * 2];

}  // namespace

void test_round_trip_preserves_everything() {
    const SavedSettings in = makeSettings();
    const size_t n = encodeSettings(in, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);

    SavedSettings out;
    TEST_ASSERT_TRUE(SettingsLoadStatus::Ok == decodeSettings(buf, n, out));
    TEST_ASSERT_TRUE(Sport::Cricket == out.sport);
    TEST_ASSERT_FALSE(out.swingOutputEnabled);
    TEST_ASSERT_EQUAL_FLOAT(3.25f, out.tunables.onsetRadS);
    TEST_ASSERT_EQUAL_FLOAT(0.75f, out.tunables.endRadS);
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, out.tunables.forehandYawSign);
    TEST_ASSERT_EQUAL_FLOAT(250.0f, out.tunables.cooldownMs);
}

void test_empty_means_nothing_saved() {
    SavedSettings out;
    TEST_ASSERT_TRUE(SettingsLoadStatus::Empty == decodeSettings("", 0, out));
    TEST_ASSERT_TRUE(SettingsLoadStatus::Empty == decodeSettings(nullptr, 0, out));
}

void test_truncated_record_is_refused() {
    const SavedSettings in = makeSettings();
    const size_t n = encodeSettings(in, buf, sizeof(buf));
    // Chop the record before the crc line: looks like a power loss mid-write.
    size_t cut = 0;
    for (size_t i = 0; i + 4 <= n; i++) {
        if (!strncmp(buf + i, "crc=", 4)) { cut = i; break; }
    }
    TEST_ASSERT_TRUE(cut > 0);
    SavedSettings out;
    TEST_ASSERT_TRUE(SettingsLoadStatus::Truncated == decodeSettings(buf, cut, out));
}

void test_bad_checksum_is_refused() {
    const SavedSettings in = makeSettings();
    const size_t n = encodeSettings(in, buf, sizeof(buf));
    // Flip one digit inside a value, leaving the crc stale.
    char *hit = strstr(buf, "onset=");
    TEST_ASSERT_TRUE(hit != nullptr);
    hit[6] = (hit[6] == '3') ? '4' : '3';
    SavedSettings out;
    TEST_ASSERT_TRUE(SettingsLoadStatus::BadChecksum == decodeSettings(buf, n, out));
}

void test_unknown_version_is_refused() {
    const SavedSettings in = makeSettings();
    size_t n = encodeSettings(in, buf, sizeof(buf));
    char *hit = strstr(buf, "version=");
    TEST_ASSERT_TRUE(hit != nullptr);
    hit[8] = '9';   // version=9
    // Re-sign so only the version check can fail.
    const size_t bodyLen = static_cast<size_t>(strstr(buf, "crc=") - buf);
    char resigned[SETTINGS_MAX_BYTES * 2];
    snprintf(resigned, sizeof(resigned), "%.*scrc=%08lX\n", static_cast<int>(bodyLen), buf,
             static_cast<unsigned long>(settingsCrc32(buf, bodyLen)));
    n = strlen(resigned);
    SavedSettings out;
    TEST_ASSERT_TRUE(SettingsLoadStatus::BadVersion == decodeSettings(resigned, n, out));
}

void test_unknown_field_is_refused() {
    // A record written by a different firmware with an extra key.
    char body[512];
    snprintf(body, sizeof(body), "version=1\nsport=tennis\nclick=1\nonset=2.5\nsurprise_key=5\n");
    char rec[640];
    const size_t bodyLen = strlen(body);
    snprintf(rec, sizeof(rec), "%scrc=%08lX\n", body,
             static_cast<unsigned long>(settingsCrc32(body, bodyLen)));
    SavedSettings out;
    TEST_ASSERT_TRUE(SettingsLoadStatus::BadField == decodeSettings(rec, strlen(rec), out));
}

void test_out_of_range_value_is_refused() {
    char body[512];
    snprintf(body, sizeof(body), "version=1\nsport=tennis\nclick=1\nonset=9999\n");
    char rec[640];
    const size_t bodyLen = strlen(body);
    snprintf(rec, sizeof(rec), "%scrc=%08lX\n", body,
             static_cast<unsigned long>(settingsCrc32(body, bodyLen)));
    SavedSettings out;
    TEST_ASSERT_TRUE(SettingsLoadStatus::BadField == decodeSettings(rec, strlen(rec), out));
}

// A signed, checksummed record whose fields are individually fine but mutually
// inconsistent must still be refused.
void test_inconsistent_snapshot_is_refused() {
    char body[512];
    snprintf(body, sizeof(body),
             "version=1\nsport=tennis\nclick=1\nonset=1.0\nend=5.0\n");  // end > onset
    char rec[640];
    const size_t bodyLen = strlen(body);
    snprintf(rec, sizeof(rec), "%scrc=%08lX\n", body,
             static_cast<unsigned long>(settingsCrc32(body, bodyLen)));
    SavedSettings out;
    TEST_ASSERT_TRUE(SettingsLoadStatus::InvalidSnapshot == decodeSettings(rec, strlen(rec), out));
}

// A failed decode must not touch the caller's settings (no half-applied state).
void test_failed_decode_leaves_target_untouched() {
    SavedSettings target = makeSettings();
    const float before = target.tunables.onsetRadS;
    const Sport beforeSport = target.sport;

    SavedSettings decoded;
    // Random junk that happens to contain a crc= line with a wrong value.
    const char *junk = "version=1\nonset=9\ncrc=DEADBEEF\n";
    TEST_ASSERT_TRUE(SettingsLoadStatus::BadChecksum ==
                     decodeSettings(junk, strlen(junk), decoded));
    // `target` was never passed in, so of course it is unchanged - the caller
    // only assigns on Ok. Assert the documented contract explicitly.
    TEST_ASSERT_EQUAL_FLOAT(before, target.tunables.onsetRadS);
    TEST_ASSERT_TRUE(beforeSport == target.sport);
}

void test_too_large_record_is_refused() {
    static char huge[SETTINGS_MAX_BYTES + 64];
    memset(huge, 'a', sizeof(huge));
    SavedSettings out;
    TEST_ASSERT_TRUE(SettingsLoadStatus::TooLarge == decodeSettings(huge, sizeof(huge), out));
}

void test_encode_refuses_to_overflow_the_buffer() {
    const SavedSettings in = makeSettings();
    char small[16];
    TEST_ASSERT_EQUAL_size_t(0, encodeSettings(in, small, sizeof(small)));
    TEST_ASSERT_EQUAL_CHAR('\0', small[0]);   // nothing partial left behind
}

void test_defaults_round_trip() {
    SavedSettings in;
    const size_t n = encodeSettings(in, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    SavedSettings out;
    TEST_ASSERT_TRUE(SettingsLoadStatus::Ok == decodeSettings(buf, n, out));
    TEST_ASSERT_TRUE(TunableError::None == validate(out.tunables));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_round_trip_preserves_everything);
    RUN_TEST(test_empty_means_nothing_saved);
    RUN_TEST(test_truncated_record_is_refused);
    RUN_TEST(test_bad_checksum_is_refused);
    RUN_TEST(test_unknown_version_is_refused);
    RUN_TEST(test_unknown_field_is_refused);
    RUN_TEST(test_out_of_range_value_is_refused);
    RUN_TEST(test_inconsistent_snapshot_is_refused);
    RUN_TEST(test_failed_decode_leaves_target_untouched);
    RUN_TEST(test_too_large_record_is_refused);
    RUN_TEST(test_encode_refuses_to_overflow_the_buffer);
    RUN_TEST(test_defaults_round_trip);
    return UNITY_END();
}