// Tunables: the runtime-editable copy of the thresholds in config.h.
//
// Why a struct of floats with a field table: the same table drives NVS
// persistence, the serial `set/get` commands and the web dashboard, so
// adding a threshold is one line here and nowhere else.
#pragma once

#include <stddef.h>

struct Tunables {
    // filter
    float filterTauS;
    float accelTrustTol;
    // swing segmentation
    float onsetRadS;
    float endRadS;
    float endHoldMs;
    float postPeakMs;
    float maxSwingMs;
    float cooldownMs;
    float minIntegralRad;
    float dominanceRatio;
    // tennis
    float tennisMinPeakRadS;
    float lobMinUpAccel;
    float serveOnsetPitchDeg;
    float forehandYawSign;
    // cricket
    float defensiveMaxPeakRadS;
    float defensiveMaxMs;
    float loftMinUpAccel;
    float pullBothSigns;
    // stick
    float stickDeadzoneDeg;
    float stickFullscaleDeg;
    float stickXSign;
    float stickYSign;

    // Values from config.h.
    static Tunables defaults();
};

struct TunableField {
    const char *key;            // short name used by serial/NVS/web (NVS keys max 15 chars)
    float Tunables::*member;
    float minValue;
    float maxValue;
};

extern const TunableField TUNABLE_FIELDS[];
extern const size_t TUNABLE_FIELD_COUNT;

// Set one field by key. Returns false if the key is unknown or the value is
// NaN/out of the allowed range (the struct is left unchanged).
bool tunablesSet(Tunables &t, const char *key, float value);
bool tunablesGet(const Tunables &t, const char *key, float &value);
