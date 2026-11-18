// parser - phone UDP payload (CSV text) -> ImuSample.
//
// The phone app format is configured in config.h (column indices, units).
// The parser must never crash or overrun on garbage: every failure becomes a
// ParseStatus the caller counts as "malformed".
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "imu_types.h"

struct CsvLayout {
    char   delimiter;
    int    colTimestamp;   // -1 = no timestamp column
    int    colAx, colAy, colAz, colGx, colGy, colGz;
    double timestampToSec;
    float  accelScale;
    float  gyroScale;

    static CsvLayout fromConfig();
    int maxColumn() const;  // highest column index that must exist
};

enum class ParseStatus : uint8_t {
    Ok = 0,
    Empty,          // blank line (not counted as malformed)
    TooFewFields,
    BadNumber,      // empty / non-numeric / trailing junk in a needed field
    OutOfRange,     // NaN/inf or beyond the sanity limits in config.h
    LineTooLong,
};

const char *parseStatusName(ParseStatus s);

// Parse one line (no newline needed; not required to be NUL-terminated).
ParseStatus parseCsvLine(const char *line, size_t len, const CsvLayout &layout, ImuSample &out);

struct PayloadResult {
    size_t      samples = 0;    // good samples written to `out`
    size_t      malformed = 0;  // non-empty lines that failed to parse
    size_t      overflow = 0;   // valid lines dropped because `out` was full
    bool        truncated = false;  // payload was cut short: nothing was parsed
    ParseStatus lastError = ParseStatus::Ok;
};

// Parse a whole datagram: zero or more '\n'-separated lines (a trailing
// newline is fine). At most `maxOut` samples are stored; extra valid lines are
// counted in `overflow` so nothing is silently lost.
//
// If `datagramTruncated` is set the payload is not parsed at all: a truncated
// datagram can look like valid CSV, so none of it can be trusted.
PayloadResult parsePayload(const uint8_t *data, size_t len, const CsvLayout &layout,
                           bool datagramTruncated, ImuSample *out, size_t maxOut);