// tunables - the runtime-editable copy of the swing/filter thresholds.
//
// The same field table drives the serial `set`/`get` commands and the dashboard,
// so adding a threshold is one line here plus its default in config.h.
//
// Validation is deliberately two-stage:
//   * tunablesSet()  - one field, range-checked, used by interactive edits.
//   * validate()     - the WHOLE snapshot, including relations between fields.
// A single field can be in range and still be inconsistent (end above onset,
// a serve threshold beyond the supported tilt), so a snapshot must be validated
// before it is applied or persisted, and a rejected snapshot must leave the
// previous one completely untouched.
//
// Runtime changes are NOT persisted here (NVS is Phase 7), so a reboot restores
// the config.h defaults.
#pragma once

#include <stddef.h>

struct Tunables {
    // segmentation
    float onsetRadS;
    float endRadS;
    float endHoldMs;
    float postPeakMs;
    float maxSwingMs;
    float cooldownMs;
    // classification
    float minIntegralRad;
    float dominanceRatio;
    float tennisMinPeakRadS;
    float lobMinUpAccel;
    float serveOnsetPitchDeg;
    float forehandYawSign;
    float maxOnsetTiltDeg;
    // cricket
    float defensiveMaxPeakRadS;
    float defensiveMaxMs;
    float loftMinUpAccel;
    float pullBothSigns;

    static Tunables defaults();
};

struct TunableField {
    const char *key;              // short name used by serial `set`/`get`
    float Tunables::*member;
    float minValue;
    float maxValue;
};

extern const TunableField TUNABLE_FIELDS[];
extern const size_t TUNABLE_FIELD_COUNT;

// Why a snapshot was rejected; used to report a useful message.
enum class TunableError {
    None = 0,
    UnknownKey,
    OutOfRange,
    NotFinite,
    EndNotBelowOnset,
    PostPeakExceedsMax,
    ServeAboveTilt,
    YawSignNotUnit,
};

const char *tunableErrorText(TunableError e);

// Single field, range only. Leaves the struct untouched when rejected.
bool tunablesSet(Tunables &t, const char *key, float value, TunableError *err = nullptr);

bool tunablesGet(const Tunables &t, const char *key, float &value);

// Full-snapshot validation: every field in range plus the cross-field rules.
// Returns TunableError::None when the snapshot is safe to apply.
TunableError validate(const Tunables &t);

// Convenience: validate a candidate before replacing `target`. On failure the
// target is left exactly as it was.
bool applyValidated(Tunables &target, const Tunables &candidate, TunableError *err = nullptr);

// Print all fields as "key=<value>" lines through `emit`, at enough precision to
// round-trip and with correct handling of small negative values.
void tunablesDump(const Tunables &t, void (*emit)(const char *line));