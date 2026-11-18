// parser - phone UDP payload (CSV text) -> ImuSample.
//
// The phone app format is configured in config.h (column indices, units).
// The parser must never crash or overrun on garbage: every failure becomes a
// ParseStatus the caller counts as "malformed".
#pragma once

#include <stddef.h>
#include <stdint.h>

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
    Empty,          // blank line (not counted as malformed by parsePayload)
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
    ParseStatus lastError = ParseStatus::Ok;
};

// Parse a whole datagram: zero or more '\n'-separated lines (a trailing
// newline is fine). At most `maxOut` samples are stored; extra lines count as
// malformed ("too many") so nothing is silently lost.
PayloadResult parsePayload(const uint8_t *data, size_t len, const CsvLayout &layout,
                           ImuSample *out, size_t maxOut);
