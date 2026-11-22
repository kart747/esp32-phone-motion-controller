#include "settings_store.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

uint32_t crcTable[16];
bool     crcTableReady = false;

void buildCrcTable() {
    for (uint32_t i = 0; i < 16; i++) {
        uint32_t c = i;
        for (int k = 0; k < 4; k++) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crcTable[i] = c;
    }
    crcTableReady = true;
}

// Accumulate nibble-at-a-time so the table stays tiny in RAM.
uint32_t crcUpdate(uint32_t crc, const char *data, size_t len) {
    if (!crcTableReady) buildCrcTable();
    for (size_t i = 0; i < len; i++) {
        const uint8_t b = static_cast<uint8_t>(data[i]);
        crc = crcTable[(crc ^ b) & 0x0F] ^ (crc >> 4);
        crc = crcTable[(crc ^ (b >> 4)) & 0x0F] ^ (crc >> 4);
    }
    return crc;
}

}  // namespace

uint32_t settingsCrc32(const char *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    crc = crcUpdate(crc, data, len);
    return crc ^ 0xFFFFFFFFu;
}

const char *settingsLoadStatusText(SettingsLoadStatus s) {
    switch (s) {
        case SettingsLoadStatus::Ok:              return "ok";
        case SettingsLoadStatus::Empty:           return "nothing saved";
        case SettingsLoadStatus::TooLarge:        return "record too large";
        case SettingsLoadStatus::Truncated:       return "record truncated";
        case SettingsLoadStatus::BadVersion:      return "unknown version";
        case SettingsLoadStatus::BadChecksum:     return "checksum mismatch";
        case SettingsLoadStatus::BadField:        return "bad field";
        case SettingsLoadStatus::InvalidSnapshot: return "settings combination rejected";
    }
    return "?";
}

size_t encodeSettings(const SavedSettings &s, char *out, size_t cap) {
    if (!out || cap == 0) return 0;

    char body[SETTINGS_MAX_BYTES];
    int n = snprintf(body, sizeof(body), "version=%lu\nsport=%s\nclick=%d\n",
                     static_cast<unsigned long>(SETTINGS_VERSION), sportName(s.sport),
                     s.swingOutputEnabled ? 1 : 0);
    if (n < 0 || static_cast<size_t>(n) >= sizeof(body)) return 0;

    for (size_t i = 0; i < TUNABLE_FIELD_COUNT; i++) {
        char line[64];
        const int w = snprintf(line, sizeof(line), "%s=%.4g\n", TUNABLE_FIELDS[i].key,
                               static_cast<double>(s.tunables.*(TUNABLE_FIELDS[i].member)));
        if (w < 0 || static_cast<size_t>(n) + static_cast<size_t>(w) >= sizeof(body)) return 0;
        memcpy(body + n, line, static_cast<size_t>(w));
        n += w;
    }
    body[n] = '\0';

    const uint32_t crc = settingsCrc32(body, static_cast<size_t>(n));
    const int total = snprintf(out, cap, "%scrc=%08lX\n", body, static_cast<unsigned long>(crc));
    if (total < 0 || static_cast<size_t>(total) >= cap) {
        out[0] = '\0';  // never leave a partial record behind
        return 0;
    }
    return static_cast<size_t>(total);
}

SettingsLoadStatus decodeSettings(const char *data, size_t len, SavedSettings &out) {
    if (!data || len == 0) return SettingsLoadStatus::Empty;
    if (len > SETTINGS_MAX_BYTES) return SettingsLoadStatus::TooLarge;

    // Find the crc line; everything before it is covered by the checksum.
    const char *crcLine = nullptr;
    size_t crcOffset = 0;
    for (size_t i = 0; i + 4 <= len; i++) {
        const bool atLineStart = (i == 0) || (data[i - 1] == '\n');
        if (atLineStart && data[i] == 'c' && data[i + 1] == 'r' && data[i + 2] == 'c' &&
            data[i + 3] == '=') {
            crcLine = data + i;
            crcOffset = i;
            break;
        }
    }
    if (!crcLine) return SettingsLoadStatus::Truncated;

    char crcHex[16] = {};
    size_t k = 0;
    for (size_t i = crcOffset + 4; i < len && data[i] != '\n' && k + 1 < sizeof(crcHex); i++) {
        crcHex[k++] = data[i];
    }
    crcHex[k] = '\0';
    char *endp = nullptr;
    const unsigned long storedCrc = strtoul(crcHex, &endp, 16);
    if (endp == crcHex || (endp && *endp != '\0')) return SettingsLoadStatus::BadChecksum;

    const uint32_t actual = settingsCrc32(data, crcOffset);
    if (actual != static_cast<uint32_t>(storedCrc)) return SettingsLoadStatus::BadChecksum;

    // Parse the body into a candidate; nothing is applied until it fully passes.
    SavedSettings cand;
    cand.tunables = Tunables::defaults();
    cand.sport = Sport::Tennis;
    cand.swingOutputEnabled = true;
    bool sawVersion = false;

    size_t start = 0;
    while (start < crcOffset) {
        size_t eol = start;
        while (eol < crcOffset && data[eol] != '\n') eol++;
        const size_t lineLen = eol - start;
        if (lineLen > 0) {
            char line[80];
            if (lineLen >= sizeof(line)) return SettingsLoadStatus::BadField;
            memcpy(line, data + start, lineLen);
            line[lineLen] = '\0';

            char *eq = strchr(line, '=');
            if (!eq) return SettingsLoadStatus::BadField;
            *eq = '\0';
            const char *key = line;
            const char *val = eq + 1;

            if (!strcmp(key, "version")) {
                sawVersion = true;
                const long v = strtol(val, &endp, 10);
                if (endp == val || (endp && *endp != '\0')) return SettingsLoadStatus::BadField;
                if (v != static_cast<long>(SETTINGS_VERSION)) {
                    return SettingsLoadStatus::BadVersion;
                }
            } else if (!strcmp(key, "sport")) {
                if (!strcmp(val, "tennis")) cand.sport = Sport::Tennis;
                else if (!strcmp(val, "cricket")) cand.sport = Sport::Cricket;
                else return SettingsLoadStatus::BadField;
            } else if (!strcmp(key, "click")) {
                const long v = strtol(val, &endp, 10);
                if (endp == val || (endp && *endp != '\0')) return SettingsLoadStatus::BadField;
                cand.swingOutputEnabled = (v != 0);
            } else {
                const float v = strtof(val, &endp);
                if (endp == val || (endp && *endp != '\0')) return SettingsLoadStatus::BadField;
                TunableError terr = TunableError::None;
                if (!tunablesSet(cand.tunables, key, v, &terr)) {
                    // Unknown key or out of range: a record from another firmware
                    // or a corrupted body. Either way, do not apply it.
                    return SettingsLoadStatus::BadField;
                }
            }
        }
        start = eol + 1;
    }

    if (!sawVersion) return SettingsLoadStatus::Truncated;
    if (validate(cand.tunables) != TunableError::None) {
        return SettingsLoadStatus::InvalidSnapshot;
    }
    out = cand;
    return SettingsLoadStatus::Ok;
}