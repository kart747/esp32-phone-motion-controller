#include "settings_nvs.h"

#include <Arduino.h>
#include <Preferences.h>

#include "config.h"

namespace {
// Separate NVS namespace so these keys cannot collide with the Wi-Fi stack.
constexpr char kNamespace[] = "motionctl";
constexpr char kKey[] = "settings";
}  // namespace

SettingsLoadStatus SettingsNvs::load(SavedSettings &out) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/true)) {
        return SettingsLoadStatus::Empty;  // namespace absent: nothing saved yet
    }
    const size_t got = prefs.getBytesLength(kKey);
    if (got == 0) {
        prefs.end();
        return SettingsLoadStatus::Empty;
    }
    if (got > sizeof(buf_)) {
        prefs.end();
        return SettingsLoadStatus::TooLarge;
    }
    const size_t read = prefs.getBytes(kKey, buf_, got);
    prefs.end();
    if (read != got) return SettingsLoadStatus::Truncated;
    return decodeSettings(buf_, read, out);
}

bool SettingsNvs::save(const SavedSettings &settings) {
    if (validate(settings.tunables) != TunableError::None) return false;  // never store junk
    const size_t n = encodeSettings(settings, buf_, sizeof(buf_));
    if (n == 0) return false;

    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) return false;
    const size_t written = prefs.putBytes(kKey, buf_, n);
    prefs.end();
    return written == n;
}

bool SettingsNvs::clear() {
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) return false;
    const bool ok = prefs.remove(kKey);
    prefs.end();
    return ok;
}