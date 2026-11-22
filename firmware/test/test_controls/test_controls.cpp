#include <math.h>
#include <string.h>
#include <unity.h>

#include "config.h"
#include "controller.h"
#include "debounce.h"
#include "led_pattern.h"

// ------------------------------------------------------------- controller

void test_begin_defaults_to_tennis_with_swing_output_enabled() {
    Controller c;
    c.begin();
    TEST_ASSERT_TRUE(Sport::Tennis == c.state().sport);
    TEST_ASSERT_TRUE(c.state().swingOutputEnabled);
    TEST_ASSERT_TRUE(TunableError::None == validate(c.state().tunables));
}

void test_toggle_sport_flips_and_back() {
    Controller c;
    c.begin();
    TEST_ASSERT_TRUE(CommandResult::Ok == c.apply(cmdToggleSport()));
    TEST_ASSERT_TRUE(Sport::Cricket == c.state().sport);
    TEST_ASSERT_TRUE(CommandResult::Ok == c.apply(cmdToggleSport()));
    TEST_ASSERT_TRUE(Sport::Tennis == c.state().sport);
}

void test_set_sport_same_value_reports_no_change() {
    Controller c;
    c.begin();
    TEST_ASSERT_TRUE(CommandResult::NoChange == c.apply(cmdSetSport(Sport::Tennis)));
    TEST_ASSERT_TRUE(CommandResult::Ok == c.apply(cmdSetSport(Sport::Cricket)));
}

void test_swing_output_toggle() {
    Controller c;
    c.begin();
    TEST_ASSERT_TRUE(c.state().swingOutputEnabled);
    TEST_ASSERT_TRUE(CommandResult::Ok == c.apply(cmdToggleSwingOutput()));
    TEST_ASSERT_FALSE(c.state().swingOutputEnabled);
    TEST_ASSERT_TRUE(CommandResult::Ok == c.apply(cmdSetSwingOutput(true)));
    TEST_ASSERT_TRUE(c.state().swingOutputEnabled);
    TEST_ASSERT_TRUE(CommandResult::NoChange == c.apply(cmdSetSwingOutput(true)));
}

void test_set_tunable_applies_valid_value() {
    Controller c;
    c.begin();
    TEST_ASSERT_TRUE(CommandResult::Ok == c.apply(cmdSetTunable("onset", 3.5f)));
    TEST_ASSERT_EQUAL_FLOAT(3.5f, c.state().tunables.onsetRadS);
}

void test_set_tunable_rejects_unknown_key_without_change() {
    Controller c;
    c.begin();
    const Tunables before = c.state().tunables;
    TEST_ASSERT_TRUE(CommandResult::InvalidKey == c.apply(cmdSetTunable("nope", 1.0f)));
    TEST_ASSERT_EQUAL_FLOAT(before.onsetRadS, c.state().tunables.onsetRadS);
}

void test_set_tunable_rejects_out_of_range_without_change() {
    Controller c;
    c.begin();
    const float before = c.state().tunables.onsetRadS;
    TEST_ASSERT_TRUE(CommandResult::InvalidValue == c.apply(cmdSetTunable("onset", 999.0f)));
    TEST_ASSERT_EQUAL_FLOAT(before, c.state().tunables.onsetRadS);
}

// A per-field-valid edit that breaks a cross-field rule must be refused as a
// whole, leaving every other field untouched.
void test_set_tunable_rejects_inconsistent_snapshot() {
    Controller c;
    c.begin();
    const Tunables before = c.state().tunables;
    // end=1.2 is in range, but raising onset to 1.0 makes end > onset.
    TEST_ASSERT_TRUE(CommandResult::InvalidSnapshot == c.apply(cmdSetTunable("onset", 1.0f)));
    TEST_ASSERT_EQUAL_FLOAT(before.onsetRadS, c.state().tunables.onsetRadS);
    TEST_ASSERT_EQUAL_FLOAT(before.endRadS, c.state().tunables.endRadS);
}

void test_yaw_sign_zero_is_rejected() {
    Controller c;
    c.begin();
    TEST_ASSERT_TRUE(CommandResult::InvalidSnapshot == c.apply(cmdSetTunable("yaw_sign", 0.0f)));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, c.state().tunables.forehandYawSign);
}

void test_reset_tunables_restores_config_defaults() {
    Controller c;
    c.begin();
    c.apply(cmdSetTunable("onset", 3.5f));
    TEST_ASSERT_TRUE(CommandResult::Ok == c.apply(cmdResetTunables()));
    TEST_ASSERT_EQUAL_FLOAT(SWING_ONSET_RAD_S, c.state().tunables.onsetRadS);
}

void test_long_key_is_truncated_safely_not_overrun() {
    Command cmd = cmdSetTunable("onset_but_this_key_is_far_too_long", 3.0f);
    TEST_ASSERT_EQUAL_size_t(COMMAND_KEY_LEN - 1, strlen(cmd.key));
    Controller c;
    c.begin();
    TEST_ASSERT_TRUE(CommandResult::InvalidKey == c.apply(cmd));
}

void test_which_commands_invalidate_a_swing() {
    TEST_ASSERT_TRUE(Controller::invalidatesSwing(CommandKind::Recalibrate));
    TEST_ASSERT_TRUE(Controller::invalidatesSwing(CommandKind::ToggleSport));
    TEST_ASSERT_TRUE(Controller::invalidatesSwing(CommandKind::SetTunable));
    TEST_ASSERT_TRUE(Controller::invalidatesSwing(CommandKind::ToggleSwingOutput));
    // Read-only requests must not disturb a swing in progress.
    TEST_ASSERT_FALSE(Controller::invalidatesSwing(CommandKind::RequestStats));
    TEST_ASSERT_FALSE(Controller::invalidatesSwing(CommandKind::RequestTunables));
}

// -------------------------------------------------------------- debounce

void test_debounce_ignores_bounce() {
    Debouncer d(BTN_DEBOUNCE_MS);
    d.update(false, 0);
    // Contact chatter for less than the debounce window.
    TEST_ASSERT_FALSE(d.update(true, 5));
    TEST_ASSERT_FALSE(d.update(false, 8));
    TEST_ASSERT_FALSE(d.update(true, 12));
    // Held long enough: one clean press.
    TEST_ASSERT_TRUE(d.update(true, 12 + BTN_DEBOUNCE_MS));
    TEST_ASSERT_TRUE(d.pressed());
}

void test_debounce_release_is_also_clean() {
    Debouncer d(BTN_DEBOUNCE_MS);
    d.update(false, 0);
    TEST_ASSERT_FALSE(d.update(true, 100));                       // press starts
    TEST_ASSERT_TRUE(d.update(true, 100 + BTN_DEBOUNCE_MS));      // confirmed
    TEST_ASSERT_TRUE(d.pressed());
    TEST_ASSERT_FALSE(d.update(false, 200));                      // release starts
    TEST_ASSERT_TRUE(d.update(false, 200 + BTN_DEBOUNCE_MS));     // confirmed
    TEST_ASSERT_FALSE(d.pressed());
}

// A button already held when the firmware boots must not read as a fresh press.
void test_button_held_at_boot_does_not_fire() {
    Debouncer d(BTN_DEBOUNCE_MS);
    TEST_ASSERT_FALSE(d.update(true, 0));       // seeding call
    TEST_ASSERT_FALSE(d.update(true, 1000));    // still held: no edge
    TEST_ASSERT_TRUE(d.pressed());              // but the state is known
    // Release, then a real press, is a genuine event. The first call that sees
    // the new level only starts the window; the edge is confirmed once the
    // window has elapsed.
    TEST_ASSERT_FALSE(d.update(false, 2000));
    TEST_ASSERT_TRUE(d.update(false, 2000 + BTN_DEBOUNCE_MS));
    TEST_ASSERT_FALSE(d.update(true, 3000));
    TEST_ASSERT_TRUE(d.update(true, 3000 + BTN_DEBOUNCE_MS));
}

void test_debounce_survives_millis_wrap() {
    Debouncer d(BTN_DEBOUNCE_MS);
    const uint32_t t0 = 0xFFFFFFF0u;
    d.update(false, t0);
    TEST_ASSERT_FALSE(d.update(true, t0 + 5));
    TEST_ASSERT_TRUE(d.update(true, t0 + 5 + BTN_DEBOUNCE_MS));  // wrapped value
}

// ------------------------------------------------------------------- LED

void test_led_error_has_highest_priority() {
    LedInputs in;
    in.error = true;
    in.calibrating = true;
    in.phoneStreaming = true;
    in.bleConnected = true;
    TEST_ASSERT_TRUE(LedState::Error == ledStateFor(in));
}

void test_led_state_selection() {
    LedInputs in;
    TEST_ASSERT_TRUE(LedState::ApUp == ledStateFor(in));
    in.bleConnected = true;
    TEST_ASSERT_TRUE(LedState::BleOnly == ledStateFor(in));
    in.phoneStreaming = true;
    TEST_ASSERT_TRUE(LedState::Running == ledStateFor(in));
    in.bleConnected = false;
    TEST_ASSERT_TRUE(LedState::Streaming == ledStateFor(in));
    in.calibrating = true;
    TEST_ASSERT_TRUE(LedState::Calibrating == ledStateFor(in));
}

void test_led_running_is_solid_and_error_blinks() {
    // Running: every slot lit.
    for (uint32_t t = 0; t < 1600; t += LED_SLOT_MS) {
        TEST_ASSERT_TRUE(ledPatternLevel(LedState::Running, t));
    }
    // Error: alternating, so it is visibly not solid.
    bool sawOn = false, sawOff = false;
    for (uint32_t t = 0; t < 1600; t += LED_SLOT_MS) {
        if (ledPatternLevel(LedState::Error, t)) sawOn = true;
        else sawOff = true;
    }
    TEST_ASSERT_TRUE(sawOn && sawOff);
}

void test_mode_overlay_flashes_then_expires() {
    bool level = false;
    // Tennis: one flash.
    TEST_ASSERT_TRUE(ledModeOverlay(Sport::Tennis, 0, level));
    TEST_ASSERT_TRUE(level);
    TEST_ASSERT_TRUE(ledModeOverlay(Sport::Tennis, LED_MODE_FLASH_ON_MS + 1, level));
    TEST_ASSERT_FALSE(level);                          // dark after the flash
    TEST_ASSERT_FALSE(ledModeOverlay(Sport::Tennis, LED_MODE_FLASH_TOTAL_MS, level));

    // Cricket: two flashes, so a second lit phase exists.
    int litPhases = 0;
    bool wasLit = false;
    for (uint32_t t = 0; t < LED_MODE_FLASH_TOTAL_MS; t += 10) {
        bool lv = false;
        ledModeOverlay(Sport::Cricket, t, lv);
        if (lv && !wasLit) litPhases++;
        wasLit = lv;
    }
    TEST_ASSERT_EQUAL_INT(2, litPhases);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_begin_defaults_to_tennis_with_swing_output_enabled);
    RUN_TEST(test_toggle_sport_flips_and_back);
    RUN_TEST(test_set_sport_same_value_reports_no_change);
    RUN_TEST(test_swing_output_toggle);
    RUN_TEST(test_set_tunable_applies_valid_value);
    RUN_TEST(test_set_tunable_rejects_unknown_key_without_change);
    RUN_TEST(test_set_tunable_rejects_out_of_range_without_change);
    RUN_TEST(test_set_tunable_rejects_inconsistent_snapshot);
    RUN_TEST(test_yaw_sign_zero_is_rejected);
    RUN_TEST(test_reset_tunables_restores_config_defaults);
    RUN_TEST(test_long_key_is_truncated_safely_not_overrun);
    RUN_TEST(test_which_commands_invalidate_a_swing);
    RUN_TEST(test_debounce_ignores_bounce);
    RUN_TEST(test_debounce_release_is_also_clean);
    RUN_TEST(test_button_held_at_boot_does_not_fire);
    RUN_TEST(test_debounce_survives_millis_wrap);
    RUN_TEST(test_led_error_has_highest_priority);
    RUN_TEST(test_led_state_selection);
    RUN_TEST(test_led_running_is_solid_and_error_blinks);
    RUN_TEST(test_mode_overlay_flashes_then_expires);
    return UNITY_END();
}