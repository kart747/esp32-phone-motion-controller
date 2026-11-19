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
constexpr uint32_t TASK_STACK_UDP       = 4096;
constexpr uint32_t TASK_STACK_PROCESS   = 6144;
constexpr uint32_t TASK_STACK_SERIAL    = 4096;
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

// --------------------------------------------------------------- Debug
constexpr bool     DUMP_RAW             = false;  // print every raw payload
constexpr bool     DUMP_HEX             = false;  // ...plus a hex view
constexpr bool     PRINT_ANGLES         = true;   // Phase 3: roll/pitch output
constexpr uint32_t DEBUG_ANGLES_PERIOD_MS = 100;  // 10 Hz
constexpr uint32_t STATS_PERIOD_MS      = 5000;   // periodic stats summary (0 = off)