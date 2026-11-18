// config.h - the single place for pins, ports, timings and thresholds.
// Rule for this project: no magic numbers in logic files; add a named
// constant here instead. Later phases append their sections below.
#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------- Serial
constexpr uint32_t SERIAL_BAUD = 115200;

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
constexpr uint32_t TASK_STACK_PROCESS   = 4096;
constexpr UBaseType_t TASK_PRIO_UDP     = 3;
constexpr UBaseType_t TASK_PRIO_PROCESS = 2;
// Pin tasks to core 1 (Arduino loop core); BLE/Wi-Fi stacks use core 0.
constexpr BaseType_t  TASK_CORE_APP     = 1;

// --------------------------------------------------------------- Debug
// Phase 1 raw dump: print also a hex view of each payload.
constexpr bool     DUMP_HEX             = false;
constexpr uint32_t STATS_PERIOD_MS      = 5000;  // periodic rx/drop summary
