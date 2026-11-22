#include "api_handler.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

// Bounded body so a hostile client cannot force a large parse.
constexpr size_t MAX_BODY = 512;

void writeSummary(char *out, size_t cap, const StatusSnapshot &s, const SavedSettings &cur,
                  bool savedDirty) {
    snprintf(out, cap,
             "{\"streaming\":%s,\"ble\":%s,\"calibrating\":%s,\"error\":%s,"
             "\"sport\":\"%s\",\"click\":%s,\"datagrams\":%lu,\"samples\":%lu,"
             "\"malformed\":%lu,\"queueDrops\":%lu,\"staleDropped\":%lu,"
             "\"pps\":%.1f,\"sps\":%.1f,\"lastGesture\":\"%s\",\"lastReason\":\"%s\","
             "\"savedDirty\":%s,\"clickEnabled\":%s}",
             s.phoneStreaming ? "true" : "false", s.bleConnected ? "true" : "false",
             s.calibrating ? "true" : "false", s.error ? "true" : "false", sportName(cur.sport),
             cur.swingOutputEnabled ? "true" : "false", static_cast<unsigned long>(s.datagrams),
             static_cast<unsigned long>(s.samples), static_cast<unsigned long>(s.malformed),
             static_cast<unsigned long>(s.queueDrops),
             static_cast<unsigned long>(s.staleDropped), s.packetsPerSec, s.samplesPerSec,
             s.lastGesture ? s.lastGesture : "", s.lastReason ? s.lastReason : "",
             savedDirty ? "true" : "false", s.clickEnabled ? "true" : "false");
}

void writeSettings(char *out, size_t cap, const SavedSettings &s, bool savedDirty) {
    int n = snprintf(out, cap, "{\"sport\":\"%s\",\"click\":%s,\"savedDirty\":%s,\"values\":{",
                     sportName(s.sport), s.swingOutputEnabled ? "true" : "false",
                     savedDirty ? "true" : "false");
    if (n < 0) return;
    for (size_t i = 0; i < TUNABLE_FIELD_COUNT; i++) {
        const int w = snprintf(out + n, cap > static_cast<size_t>(n) ? cap - n : 0,
                               "%s\"%s\":%.6g", i ? "," : "", TUNABLE_FIELDS[i].key,
                               static_cast<double>(s.tunables.*(TUNABLE_FIELDS[i].member)));
        if (w < 0 || static_cast<size_t>(n + w) >= cap) return;
        n += w;
    }
    snprintf(out + n, cap > static_cast<size_t>(n) ? cap - n : 0, "}}");
}

// Parse `key=value&key=value` into a candidate, validating at the end.
ApiStatus parseForm(const char *body, size_t len, SavedSettings &out, const char **why) {
    *why = "";
    SavedSettings cand;
    cand.tunables = Tunables::defaults();

    size_t i = 0;
    while (i < len) {
        size_t end = i;
        while (end < len && body[end] != '&') end++;
        const size_t pairLen = end - i;
        if (pairLen > 0) {
            char pair[80];
            if (pairLen >= sizeof(pair)) { *why = "field too long"; return ApiStatus::BadBody; }
            memcpy(pair, body + i, pairLen);
            pair[pairLen] = '\0';
            char *eq = strchr(pair, '=');
            if (!eq) { *why = "expected key=value"; return ApiStatus::BadBody; }
            *eq = '\0';
            const char *key = pair;
            const char *val = eq + 1;
            char *endp = nullptr;

            if (!strcmp(key, "sport")) {
                if (!strcmp(val, "tennis")) cand.sport = Sport::Tennis;
                else if (!strcmp(val, "cricket")) cand.sport = Sport::Cricket;
                else { *why = "sport must be tennis or cricket"; return ApiStatus::Rejected; }
            } else if (!strcmp(key, "click")) {
                if (!strcmp(val, "1") || !strcmp(val, "on") || !strcmp(val, "true")) {
                    cand.swingOutputEnabled = true;
                } else if (!strcmp(val, "0") || !strcmp(val, "off") || !strcmp(val, "false")) {
                    cand.swingOutputEnabled = false;
                } else {
                    *why = "click must be on or off";
                    return ApiStatus::Rejected;
                }
            } else {
                const float v = strtof(val, &endp);
                if (endp == val || (endp && *endp != '\0')) {
                    *why = "value is not a number";
                    return ApiStatus::BadBody;
                }
                TunableError terr = TunableError::None;
                if (!tunablesSet(cand.tunables, key, v, &terr)) {
                    *why = tunableErrorText(terr);
                    return (terr == TunableError::UnknownKey) ? ApiStatus::BadBody
                                                               : ApiStatus::Rejected;
                }
            }
        }
        i = end + 1;
    }
    if (validate(cand.tunables) != TunableError::None) {
        *why = "settings combination rejected";
        return ApiStatus::Rejected;
    }
    out = cand;
    return ApiStatus::Ok;
}

}  // namespace

int apiStatusCode(ApiStatus s) {
    switch (s) {
        case ApiStatus::Ok:               return 200;
        case ApiStatus::Unauthorized:     return 401;
        case ApiStatus::BadOrigin:        return 403;
        case ApiStatus::NotFound:         return 404;
        case ApiStatus::MethodNotAllowed: return 405;
        case ApiStatus::BodyTooLarge:     return 413;
        case ApiStatus::BadBody:          return 400;
        case ApiStatus::Rejected:         return 422;
    }
    return 400;
}

bool apiTokenMatches(const char *provided, size_t providedLen, const char *expected) {
    if (!expected) return true;      // auth disabled (see docs/security note)
    if (!provided) return false;
    const size_t expLen = strlen(expected);
    // Compare over the longer length so an early mismatch does not short-circuit
    // on the first differing byte.
    size_t diff = (providedLen == expLen) ? 0u : 1u;
    const size_t n = (providedLen > expLen) ? providedLen : expLen;
    for (size_t i = 0; i < n; i++) {
        const char a = (i < providedLen) ? provided[i] : '\0';
        const char b = (i < expLen) ? expected[i] : '\0';
        if (a != b) diff = 1;
    }
    return diff == 0;
}

bool apiOriginAllowed(const char *origin, const char *host) {
    if (!origin || !*origin) return true;   // non-browser client (curl)
    if (!host || !*host) return false;
    // Accept only an Origin whose host part matches the Host header. The AP is
    // reached by IP, so exact matching on the authority is enough and avoids any
    // wildcard behaviour.
    const char *schemeEnd = strstr(origin, "://");
    const char *originHost = schemeEnd ? schemeEnd + 3 : origin;
    return strcmp(originHost, host) == 0;
}

ApiResult handleApi(const ApiRequest &req, const StatusSnapshot &status, const SavedSettings &current,
                    bool savedDirty, char *out, size_t cap) {
    ApiResult r;
    if (!out || cap == 0) return r;

    const bool isGet = !strcmp(req.method, "GET");
    const bool isPost = !strcmp(req.method, "POST");

    if (!isGet && !isPost) {
        r.status = ApiStatus::MethodNotAllowed;
        return r;
    }

    // ---- reads
    if (!strcmp(req.path, "/api/status")) {
        if (!isGet) { r.status = ApiStatus::MethodNotAllowed; return r; }
        writeSummary(out, cap, status, current, savedDirty);
        r.status = ApiStatus::Ok;
        r.bodyLen = strlen(out);
        return r;
    }

    if (!strcmp(req.path, "/api/settings")) {
        if (isGet) {
            writeSettings(out, cap, current, savedDirty);
            r.status = ApiStatus::Ok;
            r.bodyLen = strlen(out);
            return r;
        }
        // ---- writes below all need authorisation
        if (!req.originOk) { r.status = ApiStatus::BadOrigin; return r; }
        if (!req.tokenOk) { r.status = ApiStatus::Unauthorized; return r; }
        if (req.bodyLen > MAX_BODY) { r.status = ApiStatus::BodyTooLarge; return r; }
        if (!req.body || req.bodyLen == 0) { r.status = ApiStatus::BadBody; return r; }

        const char *why = "";
        SavedSettings cand;
        const ApiStatus st = parseForm(req.body, req.bodyLen, cand, &why);
        if (st != ApiStatus::Ok) {
            snprintf(out, cap, "{\"ok\":false,\"error\":\"%s\"}", why);
            r.status = st;
            r.bodyLen = strlen(out);
            return r;
        }
        r.hasSettings = true;
        r.settings = cand;
        snprintf(out, cap, "{\"ok\":true}");
        r.status = ApiStatus::Ok;
        r.bodyLen = strlen(out);
        return r;
    }

    if (!strcmp(req.path, "/api/defaults")) {
        if (!isPost) { r.status = ApiStatus::MethodNotAllowed; return r; }
        if (!req.originOk) { r.status = ApiStatus::BadOrigin; return r; }
        if (!req.tokenOk) { r.status = ApiStatus::Unauthorized; return r; }
        r.hasSettings = true;
        r.settings = SavedSettings{};   // defaults incl. tennis + click on
        snprintf(out, cap, "{\"ok\":true,\"note\":\"defaults applied; save to keep them\"}");
        r.status = ApiStatus::Ok;
        r.bodyLen = strlen(out);
        return r;
    }

    if (!strcmp(req.path, "/api/save")) {
        if (!isPost) { r.status = ApiStatus::MethodNotAllowed; return r; }
        if (!req.originOk) { r.status = ApiStatus::BadOrigin; return r; }
        if (!req.tokenOk) { r.status = ApiStatus::Unauthorized; return r; }
        r.wantsSave = true;
        snprintf(out, cap, "{\"ok\":true}");
        r.status = ApiStatus::Ok;
        r.bodyLen = strlen(out);
        return r;
    }

    r.status = ApiStatus::NotFound;
    return r;
}