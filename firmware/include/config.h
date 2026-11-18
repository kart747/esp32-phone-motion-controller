// config.h - the single place for pins, ports, timings and thresholds.
//
// Rules for this project:
//  * No magic numbers in logic files: add a named constant here instead.
//  * This header is plain C++ (no Arduino includes) so the hardware-free
//    logic in lib/motion can be unit-tested on the laptop (`pio test -e native`).
//  * Gesture/filter/stick values marked "tunable" are only the DEFAULTS. At
//    runtime they live in `Tunables` (lib/motion/tunables.h), can be changed
//    over serial (`set <key> <value>`) or the web dashboard, and are stored
//    in NVS. IMPORTANT: the default numbers are initial engineering guesses,
//    not measured values. Collect data (tuning mode) and adjust - see
//    docs/testing.md.
#pragma once

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------- Serial
constexpr uint32_t SERIAL_BAUD = 115200;
constexpr size_t   SERIAL_CMD_MAX_LEN = 96;   // longest accepted command line

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
constexpr bool    LED_ACTIVE_HIGH  = true;

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
constexpr size_t   OUT_QUEUE_LEN        = 16;    // commands to gamepad task
constexpr size_t   CTRL_QUEUE_LEN       = 8;     // button events to process task
constexpr uint32_t TASK_STACK_UDP       = 4096;
constexpr uint32_t TASK_STACK_PROCESS   = 6144;
constexpr uint32_t TASK_STACK_GAMEPAD   = 4096;
constexpr uint32_t TASK_STACK_BUTTONS   = 2048;
constexpr uint32_t TASK_STACK_LED       = 2048;
constexpr uint32_t TASK_STACK_SERIAL    = 4096;
constexpr uint32_t TASK_STACK_DASHBOARD = 6144;
constexpr unsigned TASK_PRIO_UDP        = 4;
constexpr unsigned TASK_PRIO_PROCESS    = 3;
constexpr unsigned TASK_PRIO_GAMEPAD    = 3;
constexpr unsigned TASK_PRIO_BUTTONS    = 2;
constexpr unsigned TASK_PRIO_LED        = 1;
constexpr unsigned TASK_PRIO_SERIAL     = 1;
constexpr unsigned TASK_PRIO_DASHBOARD  = 1;
// App tasks run on core 1 (Arduino loop core); Wi-Fi/BLE stacks use core 0.
constexpr int      TASK_CORE_APP        = 1;
constexpr uint32_t PROCESS_WAKE_MS      = 10;    // process task wake period (timeout checks)

// ------------------------------------------------------- Phone packet format
// CSV, one sample per line. Column indices are 0-based; -1 = column absent.
// Extra columns are ignored. Update after looking at the raw dump (`raw on`).
constexpr char     CSV_DELIMITER        = ',';
constexpr int      CSV_COL_TIMESTAMP    = 0;
constexpr int      CSV_COL_AX           = 1;
constexpr int      CSV_COL_AY           = 2;
constexpr int      CSV_COL_AZ           = 3;
constexpr int      CSV_COL_GX           = 4;
constexpr int      CSV_COL_GY           = 5;
constexpr int      CSV_COL_GZ           = 6;
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
constexpr uint32_t DATA_TIMEOUT_MS      = 500;   // no data this long -> release all inputs
constexpr uint32_t RATE_WINDOW_MS       = 1000;  // packet-rate averaging window
constexpr float    NOMINAL_SAMPLE_RATE_HZ = 100.0f;  // used if timestamps are unusable
constexpr float    DT_MAX_S             = 0.1f;  // larger gaps are treated as glitches

// ------------------------------------------------------------ Calibration
// Hold the phone still in the neutral pose for CAL_DURATION_MS.
constexpr uint32_t CAL_DURATION_MS      = 1500;
constexpr uint32_t CAL_MIN_SAMPLES      = 30;    // below this the window is rejected
constexpr float    CAL_MAX_GYRO_STD     = 0.08f; // rad/s, std-dev (combined) allowed while "still"
constexpr float    CAL_MAX_ACCEL_STD    = 0.6f;  // m/s^2
constexpr float    CAL_ABORT_GYRO_RAD_S = 1.0f;  // any sample above this restarts the window at once
constexpr float    CAL_GRAVITY_MIN_MS2  = 7.0f;  // mean |accel| must look like 1 g...
constexpr float    CAL_GRAVITY_MAX_MS2  = 13.0f; // ...otherwise units are probably wrong
constexpr uint32_t CAL_ERROR_AFTER_ATTEMPTS = 10;  // failed windows before LED error

// ---------------------------------------------------- Filter (tunable defaults)
constexpr float    FILTER_TAU_S         = 0.6f;  // accel->gravity blend time constant
constexpr float    ACCEL_TRUST_TOL_MS2  = 1.5f;  // ignore accel when | |a| - g | exceeds this

// ------------------------------------------------ Swing detection (tunable defaults)
constexpr float    SWING_ONSET_RAD_S    = 2.5f;  // gyro magnitude that starts a capture
constexpr float    SWING_END_RAD_S      = 1.2f;  // below this counts as "quiet"
constexpr float    SWING_END_HOLD_MS    = 40.0f; // quiet this long ends the capture
constexpr float    SWING_POST_PEAK_MS   = 150.0f;// capture ends this long after the peak
constexpr float    SWING_MAX_MS         = 500.0f;// hard cap on capture length
constexpr float    SWING_COOLDOWN_MS    = 400.0f;// ignore new swings after one ends
constexpr float    SWING_MIN_INTEGRAL_RAD = 0.5f;// dominant-axis rotation needed (rad)
constexpr float    SWING_DOMINANCE_RATIO  = 1.3f;// dominant axis must beat the next by this factor
// Tennis
constexpr float    TENNIS_MIN_PEAK_RAD_S   = 4.0f;  // weaker swings are ignored in tennis
constexpr float    LOB_MIN_UP_ACCEL_MS2    = 6.0f;  // "strong upward component"
constexpr float    SERVE_ONSET_PITCH_MIN_DEG = 40.0f;// phone raised (nose up) this much at onset
constexpr float    FOREHAND_YAW_SIGN       = 1.0f;  // +1 right-hander, -1 left-hander
// Cricket
constexpr float    DEFENSIVE_MAX_PEAK_RAD_S = 5.0f; // low-energy shot
constexpr float    DEFENSIVE_MAX_MS         = 300.0f;
constexpr float    LOFT_MIN_UP_ACCEL_MS2    = 6.0f;
constexpr float    PULL_BOTH_SIGNS          = 1.0f; // 1 = horizontal swing either way is a pull

// ------------------------------------------------ Analog stick (tunable defaults)
constexpr float    STICK_DEADZONE_DEG   = 5.0f;  // tilt ignored below this
constexpr float    STICK_FULLSCALE_DEG  = 35.0f; // tilt for full deflection
constexpr float    STICK_X_SIGN         = 1.0f;  // +1: tilt right -> stick right
constexpr float    STICK_Y_SIGN         = 1.0f;  // +1: nose down -> stick up (forward)
constexpr float    GRAVITY_MS2          = 9.80665f;

// ----------------------------------------------------------------- Gamepad
constexpr char     GAMEPAD_NAME[]         = "MotionCtl Pad";
constexpr char     GAMEPAD_MANUFACTURER[] = "DIY";
constexpr uint16_t GAMEPAD_BUTTON_COUNT   = 16;
constexpr int16_t  GAMEPAD_AXIS_MAX       = 32767;   // library default range is -32768..32767
constexpr uint32_t GESTURE_PRESS_MS       = 80;      // button hold time per gesture
constexpr uint32_t STICK_REPORT_INTERVAL_MS = 20;    // max 50 stick reports per second
constexpr uint32_t GAMEPAD_TASK_WAKE_MS   = 5;
// Gesture -> gamepad button number (1-based, BUTTON_1 == 1).
constexpr uint8_t  BTN_TENNIS_FOREHAND    = 1;
constexpr uint8_t  BTN_TENNIS_BACKHAND    = 2;
constexpr uint8_t  BTN_TENNIS_LOB         = 3;
constexpr uint8_t  BTN_TENNIS_SERVE       = 4;
constexpr uint8_t  BTN_CRICKET_DRIVE      = 1;
constexpr uint8_t  BTN_CRICKET_PULL       = 2;
constexpr uint8_t  BTN_CRICKET_LOFT       = 3;
constexpr uint8_t  BTN_CRICKET_DEFENSIVE  = 4;

// ----------------------------------------------------------------- Buttons
constexpr uint32_t BTN_POLL_MS            = 5;
constexpr uint32_t BTN_DEBOUNCE_MS        = 30;

// --------------------------------------------------------------- Status LED
// Pattern = 16 slots of LED_SLOT_MS each (bit 0 first), 1 = LED on.
constexpr uint32_t LED_SLOT_MS            = 125;
constexpr uint16_t LED_PAT_ERROR          = 0x5555;  // fast blink
constexpr uint16_t LED_PAT_CALIBRATING    = 0x3333;  // medium blink
constexpr uint16_t LED_PAT_AP_UP          = 0x0001;  // one short blip every 2 s
constexpr uint16_t LED_PAT_STREAMING      = 0x0005;  // double blip every 2 s (phone data, no BLE)
constexpr uint16_t LED_PAT_BLE_ONLY       = 0x00FF;  // 1 s on / 1 s off (BLE up, no phone data)
constexpr uint16_t LED_PAT_RUNNING        = 0xFFFF;  // solid: phone streaming AND BLE connected
constexpr uint32_t LED_TASK_MS            = 25;
constexpr uint32_t LED_MODE_FLASH_TOTAL_MS  = 1200;  // overlay length after a mode change
constexpr uint32_t LED_MODE_FLASH_ON_MS     = 150;
constexpr uint32_t LED_MODE_FLASH_PERIOD_MS = 400;   // tennis = 1 flash, cricket = 2

// --------------------------------------------------------- Dashboard / NVS
constexpr bool     ENABLE_DASHBOARD       = true;
constexpr uint16_t HTTP_PORT              = 80;
constexpr uint32_t DASHBOARD_POLL_MS      = 5;
constexpr size_t   GESTURE_HISTORY_LEN    = 10;
constexpr char     NVS_NAMESPACE[]        = "motionctl";

// ------------------------------------------------------------------- Debug
// Defaults for runtime toggles (serial commands: raw / hex / angles / tune).
constexpr bool     DEBUG_RAW_DEFAULT      = false;  // dump every raw UDP payload
constexpr bool     DEBUG_HEX_DEFAULT      = false;
constexpr bool     DEBUG_ANGLES_DEFAULT   = false;  // roll/pitch at 10 Hz
constexpr bool     DEBUG_TUNE_DEFAULT     = false;  // one CSV line per swing
constexpr uint32_t DEBUG_ANGLES_PERIOD_MS = 100;    // 10 Hz
constexpr uint32_t STATS_PERIOD_MS        = 5000;   // periodic stats summary (0 = off)
