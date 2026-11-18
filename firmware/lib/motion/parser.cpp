#include "parser.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

CsvLayout CsvLayout::fromConfig() {
    CsvLayout l;
    l.delimiter = CSV_DELIMITER;
    l.colTimestamp = CSV_COL_TIMESTAMP;
    l.colAx = CSV_COL_AX;
    l.colAy = CSV_COL_AY;
    l.colAz = CSV_COL_AZ;
    l.colGx = CSV_COL_GX;
    l.colGy = CSV_COL_GY;
    l.colGz = CSV_COL_GZ;
    l.timestampToSec = TIMESTAMP_TO_SECONDS;
    l.accelScale = ACCEL_SCALE_TO_MS2;
    l.gyroScale = GYRO_SCALE_TO_RAD_S;
    return l;
}

int CsvLayout::maxColumn() const {
    int m = colTimestamp;
    const int cols[] = {colAx, colAy, colAz, colGx, colGy, colGz};
    for (int c : cols) m = c > m ? c : m;
    return m;
}

const char *parseStatusName(ParseStatus s) {
    switch (s) {
        case ParseStatus::Ok:           return "ok";
        case ParseStatus::Empty:        return "empty";
        case ParseStatus::TooFewFields: return "too_few_fields";
        case ParseStatus::BadNumber:    return "bad_number";
        case ParseStatus::OutOfRange:   return "out_of_range";
        case ParseStatus::LineTooLong:  return "line_too_long";
    }
    return "?";
}

// Parse a trimmed numeric field. Rejects empty fields and trailing junk
// ("1.5abc") because strtod alone would accept the prefix.
static bool parseNumber(const char *s, size_t n, double &out) {
    while (n > 0 && (*s == ' ' || *s == '\t')) { s++; n--; }
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) n--;
    if (n == 0 || n >= CSV_FIELD_BUF_LEN) return false;
    char buf[CSV_FIELD_BUF_LEN];
    memcpy(buf, s, n);
    buf[n] = '\0';
    char *end = nullptr;
    out = strtod(buf, &end);
    return end == buf + n;
}

ParseStatus parseCsvLine(const char *line, size_t len, const CsvLayout &layout, ImuSample &out) {
    if (len > CSV_MAX_LINE_LEN) return ParseStatus::LineTooLong;

    // Treat an all-whitespace line as empty.
    size_t i = 0;
    while (i < len && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) i++;
    if (i == len) return ParseStatus::Empty;

    const int needed = layout.maxColumn() + 1;
    // Split into fields lazily: remember where each *needed* field starts/ends.
    double values[7];  // 0 = timestamp, 1..3 accel, 4..6 gyro
    int    colOf[7] = {layout.colTimestamp, layout.colAx, layout.colAy, layout.colAz,
                       layout.colGx, layout.colGy, layout.colGz};
    bool   got[7] = {false, false, false, false, false, false, false};

    int    field = 0;
    size_t start = 0;
    for (size_t p = 0; p <= len; p++) {
        if (p == len || line[p] == layout.delimiter) {
            for (int k = 0; k < 7; k++) {
                if (colOf[k] == field) {
                    if (!parseNumber(line + start, p - start, values[k])) return ParseStatus::BadNumber;
                    got[k] = true;
                }
            }
            field++;
            start = p + 1;
        }
    }
    if (field < needed) return ParseStatus::TooFewFields;
    for (int k = 1; k < 7; k++) {
        if (!got[k]) return ParseStatus::TooFewFields;  // required column missing
    }

    const float a[3] = {static_cast<float>(values[1]) * layout.accelScale,
                        static_cast<float>(values[2]) * layout.accelScale,
                        static_cast<float>(values[3]) * layout.accelScale};
    const float g[3] = {static_cast<float>(values[4]) * layout.gyroScale,
                        static_cast<float>(values[5]) * layout.gyroScale,
                        static_cast<float>(values[6]) * layout.gyroScale};
    for (int k = 0; k < 3; k++) {
        if (!isfinite(a[k]) || fabsf(a[k]) > ACCEL_ABS_MAX_MS2) return ParseStatus::OutOfRange;
        if (!isfinite(g[k]) || fabsf(g[k]) > GYRO_ABS_MAX_RAD_S) return ParseStatus::OutOfRange;
    }
    if (got[0] && !isfinite(values[0])) return ParseStatus::OutOfRange;

    out.accel = {a[0], a[1], a[2]};
    out.gyro = {g[0], g[1], g[2]};
    out.hasTimestamp = got[0];
    out.tsSec = got[0] ? values[0] * layout.timestampToSec : 0.0;
    return ParseStatus::Ok;
}

PayloadResult parsePayload(const uint8_t *data, size_t len, const CsvLayout &layout,
                           ImuSample *out, size_t maxOut) {
    PayloadResult r;
    size_t lineStart = 0;
    for (size_t p = 0; p <= len; p++) {
        if (p == len || data[p] == '\n') {
            const size_t n = p - lineStart;
            if (n > 0 || p < len) {
                ImuSample s;
                const ParseStatus st =
                    parseCsvLine(reinterpret_cast<const char *>(data) + lineStart, n, layout, s);
                if (st == ParseStatus::Ok) {
                    if (r.samples < maxOut) {
                        out[r.samples++] = s;
                    } else {
                        r.malformed++;  // more lines than we can store
                        r.lastError = ParseStatus::LineTooLong;
                    }
                } else if (st != ParseStatus::Empty) {
                    r.malformed++;
                    r.lastError = st;
                }
            }
            lineStart = p + 1;
        }
    }
    return r;
}
