#include "dashboard.h"

#include <Arduino.h>
#include <WebServer.h>

#include <string.h>

#include "config.h"
#include "dashboard_html.h"

namespace {

WebServer s_server(HTTP_PORT);

// Set by poll() before servicing requests; only the web task reads them.
Dashboard::SubmitFn s_submit = nullptr;
const StatusSnapshot *s_status = nullptr;
const SavedSettings *s_settings = nullptr;
const bool *s_savedDirty = nullptr;

// Response body buffer: the API produces JSON far smaller than this; the page
// itself is sent separately.
char s_body[1024];

bool checkToken() {
    if (DASHBOARD_TOKEN[0] == '\0') return true;  // auth disabled by config
    if (!s_server.hasHeader("Authorization")) return false;
    const String header = s_server.header("Authorization");
    if (!header.startsWith("Bearer ")) return false;
    const String token = header.substring(7);
    return apiTokenMatches(token.c_str(), token.length(), DASHBOARD_TOKEN);
}

bool checkOrigin() {
    const String host = s_server.hostHeader();
    String originStorage;
    const char *origin = nullptr;
    if (s_server.hasHeader("Origin")) {
        originStorage = s_server.header("Origin");
        origin = originStorage.c_str();
    }
    return apiOriginAllowed(origin, host.c_str());
}

void handleApiPath() {
    if (!s_submit || !s_status || !s_settings || !s_savedDirty) {
        s_server.send(503, "application/json", "{\"ok\":false,\"error\":\"not ready\"}");
        return;
    }

    String body;
    if (s_server.method() == HTTP_POST) {
        if (s_server.hasHeader("Content-Length") &&
            s_server.header("Content-Length").toInt() > static_cast<int>(API_MAX_BODY)) {
            s_server.send(413, "application/json", "{\"ok\":false,\"error\":\"body too large\"}");
            return;
        }
        body = s_server.arg("plain");
    }

    const String uri = s_server.uri();
    const bool isPost = s_server.method() == HTTP_POST;
    ApiRequest req;
    req.method = isPost ? "POST" : "GET";
    req.path = uri.c_str();
    req.body = body.length() ? body.c_str() : nullptr;
    req.bodyLen = body.length();
    req.tokenOk = checkToken();
    req.originOk = checkOrigin();

    ApiResult r = handleApi(req, *s_status, *s_settings, *s_savedDirty, s_body, sizeof(s_body));

    if (r.hasSettings) {
        // Hand the change to the owner as commands; it re-validates and is the
        // only writer of live state.
        s_submit(cmdSetSport(r.settings.sport));
        s_submit(cmdSetSwingOutput(r.settings.swingOutputEnabled));
        for (size_t i = 0; i < TUNABLE_FIELD_COUNT; i++) {
            s_submit(cmdSetTunable(TUNABLE_FIELDS[i].key,
                                   r.settings.tunables.*(TUNABLE_FIELDS[i].member)));
        }
    }
    if (r.wantsSave) s_submit(cmdSaveSettings());

    s_server.send(apiStatusCode(r.status), "application/json", s_body);
}

}  // namespace

bool Dashboard::begin(SubmitFn submit) {
    submit_ = submit;
    s_submit = submit;

    s_server.on("/", HTTP_GET, []() { s_server.send_P(200, "text/html", DASHBOARD_HTML); });
    s_server.on("/api/status", HTTP_GET, handleApiPath);
    s_server.on("/api/settings", HTTP_GET, handleApiPath);
    s_server.on("/api/settings", HTTP_POST, handleApiPath);
    s_server.on("/api/defaults", HTTP_POST, handleApiPath);
    s_server.on("/api/save", HTTP_POST, handleApiPath);
    s_server.onNotFound(
        []() { s_server.send(404, "application/json", "{\"ok\":false,\"error\":\"not found\"}"); });
    s_server.begin();
    return true;
}

void Dashboard::poll(const StatusSnapshot &status, const SavedSettings &settings,
                     bool savedDirty) {
    status_ = status;
    settings_ = settings;
    savedDirty_ = savedDirty;
    s_status = &status_;
    s_settings = &settings_;
    s_savedDirty = &savedDirty_;
    s_server.handleClient();
}