// settings_nvs - persistence of the saved settings record in NVS.
//
// A thin adapter over Arduino's Preferences, deliberately separate from
// settings_store (which owns the format and all the validation) so the format
// stays unit-testable on the laptop. Every write is checked: a failed save is
// reported to the caller rather than silently pretending the settings survive.
#pragma once

#include "settings_store.h"

class SettingsNvs {
public:
    // Loads the saved record. On any failure `out` is untouched and the reason
    // is returned; the firmware keeps its defaults.
    SettingsLoadStatus load(SavedSettings &out);

    // Serialises and stores. Returns false if the record would not fit or the
    // write failed.
    bool save(const SavedSettings &settings);

    // Clears the stored record (used by "restore defaults + save").
    bool clear();

private:
    char buf_[SETTINGS_MAX_BYTES] = {};
};