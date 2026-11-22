// config.h - the single place for pins, ports, timings and thresholds.
//
// Rules for this project:
//  * No magic numbers in logic files: add a named constant here instead.
//  * This header is plain C++ (no Arduino includes) so the hardware-free
//    logic in lib/motion can be unit-tested on the laptop (`pio test -e native`).
#pragma once

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------- Serial
constexpr uint32_t SERIAL_BAUD         = 115200;
constexpr size_t   SERIAL_CMD_MAX_LEN  = 96;  // longest accepted command line

// ------------------------------------------------------------------ Pins
// Generic ESP32 DevKit vs ESP32-S3 DevKitC. Avoid strapping pins
// (0, 2, 12, 15 on ESP32; 0, 3, 45, 46 on S3) for buttons.
// Buttons are wired to GND and use the internal pull-up (active LOW).
#if CONFIG_IDF_TARGET_ESP32S3
constexpr uint8_t PIN_LED          = 4;   // external LED + resistor
constexpr uint8_t PIN_BTN_START    = 5;
constexpr uint8_t PIN_BTN_RECAL    = 6;
constexpr uint8_t PIN_BTN_MODE     = 7;
#else  // classic ESP32 DevKit
constexpr uint8_t PIN_LED          = 2;   // on-board LED on most DevKits
constexpr uint8_t PIN_BTN_START    = 25;
constexpr uint8_t PIN_BTN_RECAL    = 26;
constexpr uint8_t PIN_BTN_MODE     = 27;
#endif

// ------------------------------------------------------------ Wi-Fi SoftAP
// The phone joins this network. WPA2 needs a password of 8..63 chars.
constexpr char     AP_SSID[]        = "MotionCtl";
constexpr char     AP_PASSWORD[]    = "swing1234";
constexpr uint8_t  AP_CHANNEL       = 1;
constexpr uint8_t  AP_MAX_CLIENTS   = 2;   // phone + one spare (e.g. laptop)
// Fixed AP address so the phone app can be pointed at a known IP.
constexpr uint8_t  AP_IP[4]         = {192, 168, 4, 1};
constexpr uint8_t  AP_GATEWAY[4]    = {192, 168, 4, 1};
constexpr uint8_t  AP_SUBNET[4]     = {255, 255, 255, 0};

// -------------------------------------------------------------------- UDP
constexpr uint16_t UDP_PORT             = 5555;  // phone app -> ESP32
constexpr size_t   UDP_MAX_PAYLOAD      = 512;   // bytes kept per packet
constexpr uint32_t UDP_POLL_INTERVAL_MS = 1;     // idle sleep of the rx task

// ------------------------------------------------------ FreeRTOS plumbing
constexpr size_t   UDP_QUEUE_LEN        = 16;    // raw packets buffered
constexpr size_t   CMD_QUEUE_LEN        = 8;     // pending commands (serial/buttons/web)
constexpr uint32_t TASK_STACK_UDP       = 4096;
constexpr uint32_t TASK_STACK_PROCESS   = 6144;
constexpr uint32_t TASK_STACK_SERIAL    = 4096;
constexpr uint32_t TASK_STACK_BUTTONS   = 2048;
constexpr unsigned TASK_PRIO_UDP        = 3;
constexpr unsigned TASK_PRIO_PROCESS    = 2;
constexpr unsigned TASK_PRIO_SERIAL     = 1;
// Pin tasks to core 1 (Arduino loop core); BLE/Wi-Fi stacks use core 0.
constexpr int      TASK_CORE_APP        = 1;
// Process task wake period: bounds how late a data timeout can be noticed.
constexpr uint32_t PROCESS_WAKE_MS      = 10;

// ------------------------------------------------------- Phone packet format
// CSV, one sample per line. Column indices are 0-based; -1 = column absent.
// Extra columns are ignored. Update after checking a real raw dump (`raw on`).
// Matches the real HyperIMU stream (docs/phone-packet-sample.txt): six columns,
// no timestamp, so sample timing comes from packet arrival time.
constexpr char     CSV_DELIMITER        = ',';
constexpr int      CSV_COL_TIMESTAMP    = -1;
constexpr int      CSV_COL_AX           = 0;
constexpr int      CSV_COL_AY           = 1;
constexpr int      CSV_COL_AZ           = 2;
constexpr int      CSV_COL_GX           = 3;
constexpr int      CSV_COL_GY           = 4;
constexpr int      CSV_COL_GZ           = 5;
// Unit conversion to what the firmware uses (m/s^2 and rad/s, seconds).
constexpr double   TIMESTAMP_TO_SECONDS = 0.001;     // 0.001 if the app sends ms
constexpr float    ACCEL_SCALE_TO_MS2   = 1.0f;      // use 9.80665 if the app sends g
constexpr float    GYRO_SCALE_TO_RAD_S  = 1.0f;      // use 0.0174533 if the app sends deg/s
// Sanity limits applied AFTER scaling; samples outside are rejected as malformed.
constexpr float    ACCEL_ABS_MAX_MS2    = 160.0f;    // ~16 g (phone sensors clip near here)
constexpr float    GYRO_ABS_MAX_RAD_S   = 40.0f;     // ~2300 deg/s
constexpr size_t   CSV_MAX_LINE_LEN     = 256;
constexpr size_t   CSV_FIELD_BUF_LEN    = 32;
constexpr size_t   CSV_MAX_SAMPLES_PER_PACKET = 8;   // lines parsed from one datagram

// ------------------------------------------------- Packet stats / timeouts
constexpr uint32_t DATA_TIMEOUT_MS      = 500;   // no valid sample this long -> safe state
constexpr uint32_t RATE_WINDOW_MS       = 1000;  // packet-rate averaging window
constexpr float    NOMINAL_SAMPLE_RATE_HZ = 100.0f;  // used if timestamps are unusable
constexpr float    DT_MAX_S             = 0.1f;  // larger gaps are treated as glitches

// ------------------------------------------------------------ Calibration
// Hold the phone flat and still in the neutral pose for CAL_DURATION_MS.
constexpr uint32_t CAL_DURATION_MS      = 1500;
constexpr uint32_t CAL_MIN_SAMPLES      = 30;    // below this the window is rejected
constexpr float    CAL_MAX_GYRO_STD     = 0.08f; // rad/s, combined std-dev allowed while "still"
constexpr float    CAL_MAX_ACCEL_STD    = 0.6f;  // m/s^2
constexpr float    CAL_ABORT_GYRO_RAD_S = 1.0f;  // any sample above this restarts the window at once
constexpr float    CAL_GRAVITY_MIN_MS2  = 7.0f;  // mean |accel| must look like 1 g...
constexpr float    CAL_GRAVITY_MAX_MS2  = 13.0f; // ...otherwise units are probably wrong
// The neutral pose is "phone flat, screen up". Refuse poses tilted further
// than this, because roll/pitch relative to a steep reference is ill-defined
// and close to the Euler singularity at 90 deg.
constexpr float    CAL_MAX_REF_TILT_DEG = 30.0f;
constexpr uint32_t CAL_ERROR_AFTER_ATTEMPTS = 10;  // failed windows before an error message

// ---------------------------------------------------- Filter (tunable defaults)
constexpr float    FILTER_TAU_S         = 0.6f;  // accel->gravity blend time constant
constexpr float    ACCEL_TRUST_TOL_MS2  = 1.5f;  // ignore accel when | |a| - g | exceeds this
// Roll degenerates as pitch approaches 90 deg (gravity along the device y
// axis). Beyond this tilt the angles are reported as unusable instead.
constexpr float    NEAR_SINGULAR_PITCH_DEG = 75.0f;

// ------------------------------------------------ Analog stick (tilt -> stick)
// Roll/pitch are already relative to the calibrated neutral pose.
constexpr float    STICK_DEADZONE_DEG   = 5.0f;   // tilt ignored below this
constexpr float    STICK_FULLSCALE_DEG  = 35.0f;  // tilt for full deflection
constexpr float    STICK_X_SIGN         = 1.0f;   // +1: tilt right -> stick right
// HID Y grows downward. +1: nose down (tilt forward) -> stick up/forward.
constexpr float    STICK_Y_SIGN         = 1.0f;

// ----------------------------------------------------------- Gamepad (BLE HID)
constexpr char     BLE_DEVICE_NAME[]    = "MotionCtl Pad";
constexpr char     BLE_MANUFACTURER[]   = "DIY";
constexpr int16_t  GAMEPAD_AXIS_MAX     = 32767;  // HID axis range is -MAX..MAX
constexpr uint32_t GAMEPAD_REPORT_INTERVAL_MS = 20;  // at most 50 stick reports/s
constexpr uint8_t  GAMEPAD_BUTTON_COUNT = 8;

// ------------------------------------------------------- Output device mode
// The board is ONE Bluetooth HID device: either the tilt-stick gamepad or the
// gyro mouse. Pick at build time (pair again with the host after switching;
// the two use different device names).
enum class OutputMode : uint8_t { Gamepad, Mouse };
constexpr OutputMode OUTPUT_MODE        = OutputMode::Mouse;

// ----------------------------------------------------- Gyro mouse (BLE HID)
constexpr char     BLE_MOUSE_NAME[]     = "MotionCtl Mouse";
// Cursor counts per radian turned. ~500 = about 9 counts per degree.
constexpr float    MOUSE_GAIN_COUNTS_PER_RAD = 500.0f;
// Turning slower than this is ignored (stops residual gyro noise creeping).
constexpr float    MOUSE_RATE_DEADZONE_RAD_S = 0.08f;
// dx = X_SIGN * (turn rate about "up"), dy = Y_SIGN * (pitch rate, gyro x).
// Turn left -> cursor left; nose up -> cursor up (screen Y grows downward).
constexpr float    MOUSE_X_SIGN         = -1.0f;
constexpr float    MOUSE_Y_SIGN         = -1.0f;
constexpr uint32_t MOUSE_REPORT_INTERVAL_MS = 8;   // ~125 reports/s
constexpr int16_t  MOUSE_MAX_STEP       = 127;     // HID relative report limit
constexpr int32_t  MOUSE_MAX_BACKLOG    = 2000;    // drop movement queued beyond this

// ------------------------------------------------- Swing detection (tennis)
// NOTE: every value below is a starting guess, NOT measured. Tune them against
// real swings (see docs/testing.md); `set <key> <value>` changes them at
// runtime.
//
// Segmentation: IDLE -> (|gyro| >= SWING_ONSET_RAD_S) CAPTURE -> classify
//            -> COOLDOWN -> (time up AND quiet) IDLE
constexpr float    SWING_ONSET_RAD_S     = 2.5f;   // starts a capture
constexpr float    SWING_END_RAD_S       = 1.2f;   // below this counts as quiet
constexpr uint32_t SWING_END_HOLD_MS     = 40;     // quiet this long ends the capture
constexpr uint32_t SWING_POST_PEAK_MS    = 150;    // ...or this long after the peak
constexpr uint32_t SWING_MAX_MS          = 500;    // hard cap on capture length
constexpr uint32_t SWING_COOLDOWN_MS     = 400;    // ignore new swings after one ends
// Classification
constexpr float    SWING_MIN_INTEGRAL_RAD = 0.5f;  // dominant-axis rotation needed
constexpr float    SWING_DOMINANCE_RATIO  = 1.3f;  // dominant must beat the next axis
constexpr float    TENNIS_MIN_PEAK_RAD_S  = 4.0f;  // weaker movements are ignored
constexpr float    LOB_MIN_UP_ACCEL_MS2   = 6.0f;  // "strong upward component"
constexpr float    SERVE_ONSET_PITCH_MIN_DEG = 40.0f;  // started raised overhead
// Handedness: +1 right-handed, -1 left-handed. Mirrors forehand/backhand.
constexpr float    FOREHAND_YAW_SIGN      = 1.0f;
// A swing that starts this far from the calibrated neutral pose is reported as
// unsupported rather than classified confidently.
constexpr float    SWING_MAX_ONSET_TILT_DEG = 60.0f;

// One left click per recognised swing in mouse mode.
constexpr uint32_t SWING_CLICK_HOLD_MS   = 80;

// Mouse click mode:
//   Stroke - one short click per RECOGNISED swing (needs tuned thresholds).
//   Speed  - left button held automatically while the phone turns fast and
//            released after it slows down; for slicing games (Fruit Ninja).
enum class MouseClickMode : uint8_t { Stroke, Speed };
constexpr MouseClickMode MOUSE_CLICK_MODE = MouseClickMode::Speed;
constexpr float    MOUSE_CLICK_ON_RAD_S  = 1.5f;   // press when turning faster than this
constexpr float    MOUSE_CLICK_OFF_RAD_S = 0.7f;   // release after slower than this...
constexpr uint32_t MOUSE_CLICK_RELEASE_HOLD_MS = 80;  // ...for this long

// ------------------------------------------------------- Buttons (Phase 6)
// Active-low: wired to GND, internal pull-up. Debounce and long-press guard.
constexpr uint32_t BTN_DEBOUNCE_MS       = 30;
// A second press within this window is ignored (double-tap protection).
constexpr uint32_t BTN_MIN_GAP_MS        = 120;

// ----------------------------------------------------------- LED (Phase 6)
// 16-slot patterns, one bit per LED_SLOT_MS. 1 = lit.
constexpr uint32_t LED_SLOT_MS           = 100;
constexpr uint16_t LED_PAT_ERROR         = 0x00FF;  // rapid blink
constexpr uint16_t LED_PAT_CALIBRATING   = 0x0F0F;  // medium blink
constexpr uint16_t LED_PAT_AP_UP         = 0x0001;  // brief flick, mostly dark
constexpr uint16_t LED_PAT_STREAMING     = 0x0F00;  // blink twice as long
constexpr uint16_t LED_PAT_BLE_ONLY      = 0x0101;  // sparse double tick
constexpr uint16_t LED_PAT_RUNNING       = 0xFFFF;  // solid on
// Mode flash after a change: N short blinks (1 = tennis, 2 = cricket).
constexpr uint32_t LED_MODE_FLASH_ON_MS    = 120;
constexpr uint32_t LED_MODE_FLASH_PERIOD_MS = 280;
constexpr uint32_t LED_MODE_FLASH_TOTAL_MS  = 700;
constexpr uint32_t LED_UPDATE_PERIOD_MS     = 20;

// --------------------------------------------------- Dashboard (Phase 7)
// Served from the same SoftAP. The page is embedded in the firmware, so no
// filesystem is needed.
constexpr bool     ENABLE_DASHBOARD     = true;
constexpr uint16_t HTTP_PORT            = 80;
constexpr size_t   API_MAX_BODY         = 512;   // largest accepted POST body
constexpr uint32_t TASK_STACK_WEB       = 8192;  // WebServer needs headroom
constexpr unsigned TASK_PRIO_WEB        = 1;     // below packet processing
constexpr uint32_t WEB_POLL_MS          = 2;     // how often the web task polls
// Bearer token required for every write (apply/save/defaults). Reads are open
// on the AP. Leave empty to disable write auth, which is ONLY sensible on a
// network you fully control - see docs/dashboard.md.
constexpr char     DASHBOARD_TOKEN[]    = "";

// ------------------------------------------------- Cricket (Phase 6)
// Also guesses. A defensive block is a short, low-energy movement; it still has
// to clear SWING_ONSET_RAD_S, so anything below that is not detected at all.
constexpr float    CRICKET_DEFENSIVE_MAX_PEAK_RAD_S = 6.0f;
constexpr uint32_t CRICKET_DEFENSIVE_MAX_MS         = 180;
// A loft needs more upward acceleration than a tennis lob (harder hit).
constexpr float    CRICKET_LOFT_MIN_UP_ACCEL_MS2    = 10.0f;
// 1 = a pull counts to either side, 0 = only the handedness-matched side.
constexpr float    CRICKET_PULL_BOTH_SIGNS          = 1.0f;

// --------------------------------------------------------------- Debug
constexpr bool     DUMP_RAW             = false;  // print every raw payload
constexpr bool     DUMP_HEX             = false;  // ...plus a hex view
constexpr bool     PRINT_ANGLES         = true;   // Phase 3: roll/pitch output
constexpr uint32_t DEBUG_ANGLES_PERIOD_MS = 100;  // 10 Hz
constexpr uint32_t STATS_PERIOD_MS      = 5000;   // periodic stats summary (0 = off)