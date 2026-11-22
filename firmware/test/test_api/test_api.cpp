#include <math.h>
#include <string.h>
#include <unity.h>

#include "api_handler.h"
#include "config.h"

namespace {

char out[1024];

StatusSnapshot liveStatus() {
    StatusSnapshot s;
    s.phoneStreaming = true;
    s.bleConnected = true;
    s.calibrating = false;
    s.sport = Sport::Cricket;
    s.datagrams = 1234;
    s.samples = 1200;
    s.malformed = 3;
    s.lastGesture = "pull";
    return s;
}

SavedSettings currentSettings() {
    SavedSettings s;
    s.sport = Sport::Cricket;
    s.tunables.onsetRadS = 3.0f;
    return s;
}

ApiRequest get(const char *path) {
    ApiRequest r;
    r.method = "GET";
    r.path = path;
    return r;
}

ApiRequest post(const char *path, const char *body, bool tokenOk = true, bool originOk = true) {
    ApiRequest r;
    r.method = "POST";
    r.path = path;
    r.body = body;
    r.bodyLen = body ? strlen(body) : 0;
    r.tokenOk = tokenOk;
    r.originOk = originOk;
    return r;
}

}  // namespace

// -------------------------------------------------------------- read routes

void test_status_reports_live_condition() {
    const ApiResult r = handleApi(get("/api/status"), liveStatus(), currentSettings(), false, out,
                                  sizeof(out));
    TEST_ASSERT_TRUE(ApiStatus::Ok == r.status);
    TEST_ASSERT_TRUE(strstr(out, "\"streaming\":true") != nullptr);
    TEST_ASSERT_TRUE(strstr(out, "\"ble\":true") != nullptr);
    TEST_ASSERT_TRUE(strstr(out, "\"sport\":\"cricket\"") != nullptr);
    TEST_ASSERT_TRUE(strstr(out, "\"datagrams\":1234") != nullptr);
    TEST_ASSERT_TRUE(strstr(out, "\"lastGesture\":\"pull\"") != nullptr);
}

void test_settings_exposes_every_tunable() {
    const ApiResult r = handleApi(get("/api/settings"), liveStatus(), currentSettings(), true, out,
                                  sizeof(out));
    TEST_ASSERT_TRUE(ApiStatus::Ok == r.status);
    for (size_t i = 0; i < TUNABLE_FIELD_COUNT; i++) {
        TEST_ASSERT_TRUE(strstr(out, TUNABLE_FIELDS[i].key) != nullptr);
    }
    TEST_ASSERT_TRUE(strstr(out, "\"savedDirty\":true") != nullptr);
}

void test_unknown_path_is_404() {
    const ApiResult r = handleApi(get("/api/nope"), liveStatus(), currentSettings(), false, out,
                                  sizeof(out));
    TEST_ASSERT_TRUE(ApiStatus::NotFound == r.status);
    TEST_ASSERT_EQUAL_INT(404, apiStatusCode(r.status));
}

void test_get_on_a_write_route_is_405() {
    const ApiResult r = handleApi(get("/api/save"), liveStatus(), currentSettings(), false, out,
                                  sizeof(out));
    TEST_ASSERT_TRUE(ApiStatus::MethodNotAllowed == r.status);
}

// ------------------------------------------------------------- write auth

void test_write_without_token_is_unauthorized() {
    const ApiResult r = handleApi(post("/api/settings", "onset=3", false), liveStatus(),
                                  currentSettings(), false, out, sizeof(out));
    TEST_ASSERT_TRUE(ApiStatus::Unauthorized == r.status);
    TEST_ASSERT_FALSE(r.hasSettings);   // nothing applied
}

void test_write_from_a_foreign_origin_is_forbidden() {
    const ApiResult r = handleApi(post("/api/settings", "onset=3", true, false), liveStatus(),
                                  currentSettings(), false, out, sizeof(out));
    TEST_ASSERT_TRUE(ApiStatus::BadOrigin == r.status);
    TEST_ASSERT_FALSE(r.hasSettings);
}

void test_reads_need_no_token() {
    const ApiRequest req = get("/api/status");
    const ApiResult r = handleApi(req, liveStatus(), currentSettings(), false, out, sizeof(out));
    TEST_ASSERT_TRUE(ApiStatus::Ok == r.status);
}

// The default AP password is shared; a page on another origin must not be able
// to change settings just because the browser has network access.
void test_origin_check_accepts_same_origin_and_curl() {
    TEST_ASSERT_TRUE(apiOriginAllowed(nullptr, "192.168.4.1"));          // curl: no Origin
    TEST_ASSERT_TRUE(apiOriginAllowed("", "192.168.4.1"));
    TEST_ASSERT_TRUE(apiOriginAllowed("http://192.168.4.1", "192.168.4.1"));
    TEST_ASSERT_FALSE(apiOriginAllowed("http://evil.example", "192.168.4.1"));
    TEST_ASSERT_FALSE(apiOriginAllowed("http://192.168.4.1", ""));
}

void test_token_compare() {
    TEST_ASSERT_TRUE(apiTokenMatches("abc123", 6, "abc123"));
    TEST_ASSERT_FALSE(apiTokenMatches("abc124", 6, "abc123"));
    TEST_ASSERT_FALSE(apiTokenMatches("abc12", 5, "abc123"));    // prefix
    TEST_ASSERT_FALSE(apiTokenMatches("abc1234", 7, "abc123"));  // extension
    TEST_ASSERT_FALSE(apiTokenMatches(nullptr, 0, "abc123"));
    TEST_ASSERT_TRUE(apiTokenMatches(nullptr, 0, nullptr));      // auth disabled
}

// ------------------------------------------------------- write validation

void test_valid_settings_post_is_accepted() {
    const ApiResult r = handleApi(post("/api/settings", "onset=3.5&min_peak=5&sport=tennis"),
                                  liveStatus(), currentSettings(), false, out, sizeof(out));
    TEST_ASSERT_TRUE(ApiStatus::Ok == r.status);
    TEST_ASSERT_TRUE(r.hasSettings);
    TEST_ASSERT_EQUAL_FLOAT(3.5f, r.settings.tunables.onsetRadS);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, r.settings.tunables.tennisMinPeakRadS);
    TEST_ASSERT_TRUE(Sport::Tennis == r.settings.sport);
    TEST_ASSERT_TRUE(strstr(out, "\"ok\":true") != nullptr);
}

void test_click_flag_can_be_set_via_api() {
    const ApiResult r = handleApi(post("/api/settings", "click=off"), liveStatus(),
                                  currentSettings(), false, out, sizeof(out));
    TEST_ASSERT_TRUE(ApiStatus::Ok == r.status);
    TEST_ASSERT_FALSE(r.settings.swingOutputEnabled);
}

void test_unknown_key_is_bad_body() {
    const ApiResult r = handleApi(post("/api/settings", "wibble=1"), liveStatus(),
                                  currentSettings(), false, out, sizeof(out));
    TEST_ASSERT_TRUE(ApiStatus::BadBody == r.status);
    TEST_ASSERT_FALSE(r.hasSettings);
    TEST_ASSERT_TRUE(strstr(out, "unknown key") != nullptr);
}

void test_out_of_range_is_rejected_with_reason() {
    const ApiResult r = handleApi(post("/api/settings", "onset=9999"), liveStatus(),
                                  currentSettings(), false, out, sizeof(out));
    TEST_ASSERT_TRUE(ApiStatus::Rejected == r.status);
    TEST_ASSERT_FALSE(r.hasSettings);
    TEST_ASSERT_TRUE(strstr(out, "out of range") != nullptr);
}

// A combination that is only invalid as a whole must be refused here too; the
// device must never be left in an inconsistent state by the UI.
void test_inconsistent_combination_is_rejected() {
    const ApiResult r = handleApi(post("/api/settings", "onset=1.0&end=5.0"), liveStatus(),
                                  currentSettings(), false, out, sizeof(out));
    TEST_ASSERT_TRUE(ApiStatus::Rejected == r.status);
    TEST_ASSERT_FALSE(r.hasSettings);
    TEST_ASSERT_TRUE(strstr(out, "combination") != nullptr);
}

void test_malformed_body_is_bad_body() {
    TEST_ASSERT_TRUE(ApiStatus::BadBody ==
                     handleApi(post("/api/settings", "onset"), liveStatus(), currentSettings(),
                               false, out, sizeof(out)).status);
    TEST_ASSERT_TRUE(ApiStatus::BadBody ==
                     handleApi(post("/api/settings", "onset=abc"), liveStatus(), currentSettings(),
                               false, out, sizeof(out)).status);
    TEST_ASSERT_TRUE(ApiStatus::BadBody ==
                     handleApi(post("/api/settings", ""), liveStatus(), currentSettings(), false,
                               out, sizeof(out)).status);
}

void test_oversized_body_is_refused_before_parsing() {
    static char big[1024];
    memset(big, 'a', sizeof(big));
    big[sizeof(big) - 1] = '\0';
    const ApiResult r = handleApi(post("/api/settings", big), liveStatus(), currentSettings(),
                                  false, out, sizeof(out));
    TEST_ASSERT_TRUE(ApiStatus::BodyTooLarge == r.status);
    TEST_ASSERT_FALSE(r.hasSettings);
}

void test_defaults_route_returns_factory_settings() {
    const ApiResult r = handleApi(post("/api/defaults", nullptr), liveStatus(), currentSettings(),
                                  false, out, sizeof(out));
    TEST_ASSERT_TRUE(ApiStatus::Ok == r.status);
    TEST_ASSERT_TRUE(r.hasSettings);
    TEST_ASSERT_TRUE(Sport::Tennis == r.settings.sport);
    TEST_ASSERT_TRUE(r.settings.swingOutputEnabled);
    TEST_ASSERT_EQUAL_FLOAT(SWING_ONSET_RAD_S, r.settings.tunables.onsetRadS);
}

void test_save_route_requests_persistence() {
    const ApiResult r = handleApi(post("/api/save", nullptr), liveStatus(), currentSettings(),
                                  false, out, sizeof(out));
    TEST_ASSERT_TRUE(ApiStatus::Ok == r.status);
    TEST_ASSERT_TRUE(r.wantsSave);
    TEST_ASSERT_FALSE(r.hasSettings);
}

void test_save_requires_authorisation() {
    TEST_ASSERT_TRUE(ApiStatus::Unauthorized ==
                     handleApi(post("/api/save", nullptr, false), liveStatus(), currentSettings(),
                               false, out, sizeof(out)).status);
}

// A tiny output buffer must never overrun; the handler just truncates.
void test_small_buffer_is_safe() {
    static char tiny[64];
    const ApiResult r = handleApi(get("/api/settings"), liveStatus(), currentSettings(), false,
                                  tiny, sizeof(tiny));
    TEST_ASSERT_TRUE(ApiStatus::Ok == r.status);
    TEST_ASSERT_TRUE(r.bodyLen < sizeof(tiny));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_status_reports_live_condition);
    RUN_TEST(test_settings_exposes_every_tunable);
    RUN_TEST(test_unknown_path_is_404);
    RUN_TEST(test_get_on_a_write_route_is_405);
    RUN_TEST(test_write_without_token_is_unauthorized);
    RUN_TEST(test_write_from_a_foreign_origin_is_forbidden);
    RUN_TEST(test_reads_need_no_token);
    RUN_TEST(test_origin_check_accepts_same_origin_and_curl);
    RUN_TEST(test_token_compare);
    RUN_TEST(test_valid_settings_post_is_accepted);
    RUN_TEST(test_click_flag_can_be_set_via_api);
    RUN_TEST(test_unknown_key_is_bad_body);
    RUN_TEST(test_out_of_range_is_rejected_with_reason);
    RUN_TEST(test_inconsistent_combination_is_rejected);
    RUN_TEST(test_malformed_body_is_bad_body);
    RUN_TEST(test_oversized_body_is_refused_before_parsing);
    RUN_TEST(test_defaults_route_returns_factory_settings);
    RUN_TEST(test_save_route_requests_persistence);
    RUN_TEST(test_save_requires_authorisation);
    RUN_TEST(test_small_buffer_is_safe);
    return UNITY_END();
}