// settings_store - versioned, checksummed text record for the saved settings.
//
// Text (not binary) on purpose: it survives inspection in a serial dump, and a
// corrupt record fails loudly instead of decoding into plausible garbage.
//
//     version=1
//     sport=cricket
//     click=1
//     onset=2.5
//     ... one line per tunable ...
//     crc=1A2B3C4D
//
// The CRC covers every byte before the `crc=` line. Loading validates, in
// order: the record is complete, the version is known, the CRC matches, every
// field parses, and the resulting snapshot passes the full cross-field
// validation. Any failure reports a specific reason and the caller keeps its
// current settings - a half-written or stale record can never be applied.
//
// Hardware-free: the caller supplies the bytes and a storage backend, so the
// whole save/load/recover path is unit-testable on the laptop.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "gestures.h"
#include "tunables.h"

constexpr uint32_t SETTINGS_VERSION = 1;
// Big enough for 17 tunables + header + crc with room to spare, but bounded so a
// corrupt length cannot run away.
constexpr size_t   SETTINGS_MAX_BYTES = 768;

struct SavedSettings {
    Sport    sport = Sport::Tennis;
    bool     swingOutputEnabled = true;
    Tunables tunables = Tunables::defaults();
};

enum class SettingsLoadStatus : uint8_t {
    Ok = 0,
    Empty,        // nothing saved yet: use defaults
    TooLarge,
    Truncated,    // no crc line / incomplete
    BadVersion,
    BadChecksum,
    BadField,     // unparseable or unknown key
    InvalidSnapshot,
};

const char *settingsLoadStatusText(SettingsLoadStatus s);

// Serialise into `out`. Returns the number of bytes written, or 0 if it does
// not fit (then nothing is written).
size_t encodeSettings(const SavedSettings &s, char *out, size_t cap);

// Parse and fully validate. `out` is only modified on Ok.
SettingsLoadStatus decodeSettings(const char *data, size_t len, SavedSettings &out);

// CRC-32 (IEEE) over a byte range; exposed for tests.
uint32_t settingsCrc32(const char *data, size_t len);