// api_handler - the dashboard's HTTP surface, as pure logic.
//
// Kept free of Arduino/WebServer types so the whole route table, authentication
// and validation path can be unit-tested on the laptop. `src/dashboard.cpp` is
// a thin adapter that turns a real HTTP request into an ApiRequest and writes
// the returned body back.
//
// Routes:
//   GET  /api/status    live condition + counters
//   GET  /api/settings  current sport/click/tunables + whether they are saved
//   POST /api/settings  apply `key=value&key=value` (validated as a snapshot)
//   POST /api/defaults  apply factory defaults (still needs an explicit save)
//   POST /api/save      persist the current settings
//
// Writes require a bearer token and a trusted Origin (when one is sent), so a
// page on another origin cannot reconfigure the device from the user's browser.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "gestures.h"
#include "settings_store.h"
#include "tunables.h"

enum class ApiStatus : uint8_t {
    Ok = 0,
    Unauthorized,
    BadOrigin,
    NotFound,
    MethodNotAllowed,
    BodyTooLarge,
    BadBody,
    Rejected,      // validation failed; body explains why
};

struct ApiRequest {
    const char *method = "GET";   // "GET" or "POST"
    const char *path = "/";
    const char *body = nullptr;   // POST body (may be null)
    size_t      bodyLen = 0;
    bool        tokenOk = false;  // caller already compared the bearer token
    bool        originOk = true;  // caller checked Origin/Host (true when absent)
    bool        authorized = true;  // device configured with auth? (see docs)
};

// What the firmware reports about itself. Copied, not referenced, so the caller
// can build it from live state without lifetime concerns.
struct StatusSnapshot {
    bool     phoneStreaming = false;
    bool     bleConnected = false;
    bool     calibrating = false;
    bool     error = false;
    Sport    sport = Sport::Tennis;
    bool     clickEnabled = true;
    uint32_t datagrams = 0;
    uint32_t samples = 0;
    uint32_t malformed = 0;
    uint32_t queueDrops = 0;
    uint32_t staleDropped = 0;
    float    packetsPerSec = 0.0f;
    float    samplesPerSec = 0.0f;
    const char *lastGesture = "";
    const char *lastReason = "";
};

struct ApiResult {
    ApiStatus status = ApiStatus::NotFound;
    size_t    bodyLen = 0;   // bytes written into the caller's buffer
    // Set when the request changed the desired settings; the caller applies
    // them through the command queue (the processing task stays the owner).
    bool          hasSettings = false;
    SavedSettings settings;
    bool          wantsSave = false;
};

// Handle one request. Writes the response body (JSON or empty) into `out`.
ApiResult handleApi(const ApiRequest &req, const StatusSnapshot &status,
                    const SavedSettings &current, bool savedDirty, char *out, size_t cap);

// Constant-time-ish token comparison: length is not leaked early, so a remote
// client cannot probe the token byte by byte via timing.
bool apiTokenMatches(const char *provided, size_t providedLen, const char *expected);

// True if the request looks same-origin (or sent no Origin at all, which is the
// case for non-browser clients like curl).
bool apiOriginAllowed(const char *origin, const char *host);

int apiStatusCode(ApiStatus s);