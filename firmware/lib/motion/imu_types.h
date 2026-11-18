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
    uint32_t rxMs = 0;            // ESP32 arrival time (millis)
};

enum class GameMode : uint8_t { Tennis = 0, Cricket = 1 };

enum class Gesture : uint8_t {
    None = 0,
    Forehand, Backhand, Lob, Serve,          // tennis
    Drive, Pull, Loft, Defensive,            // cricket
    Count
};

inline const char *gestureName(Gesture g) {
    switch (g) {
        case Gesture::Forehand:  return "forehand";
        case Gesture::Backhand:  return "backhand";
        case Gesture::Lob:       return "lob";
        case Gesture::Serve:     return "serve";
        case Gesture::Drive:     return "drive";
        case Gesture::Pull:      return "pull";
        case Gesture::Loft:      return "loft";
        case Gesture::Defensive: return "defensive";
        default:                 return "none";
    }
}

inline const char *modeName(GameMode m) { return m == GameMode::Tennis ? "tennis" : "cricket"; }
