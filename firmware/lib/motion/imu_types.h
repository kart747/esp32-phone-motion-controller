// Shared data types for the motion pipeline.
#pragma once

#include <stdint.h>

#include "vec3.h"

// One parsed phone sample, already converted to SI units (see config.h scales).
struct ImuSample {
    Vec3     accel;               // m/s^2, INCLUDING gravity (Android TYPE_ACCELEROMETER)
    Vec3     gyro;                // rad/s
    bool     hasTimestamp = false;
    double   tsSec = 0.0;         // phone timestamp in seconds (valid if hasTimestamp)
    uint32_t rxMs = 0;            // ESP32 arrival time (millis), wrap-safe
};