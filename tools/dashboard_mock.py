#!/usr/bin/env python3
"""Local stand-in for the ESP32 dashboard, for browser verification.

The page itself is extracted from firmware/src/dashboard_html.h, so what is
checked here is byte-for-byte the HTML the device serves. The API is a small
Python mirror of lib/motion/api_handler.cpp with the same routes, validation
and error strings.

This does NOT test the firmware: the API logic is covered by
firmware/test/test_api (20 native cases) and the page is what is exercised in a
browser here. Use it when the device's access point is not reachable, and say
so when reporting results.

    python3 tools/dashboard_mock.py --port 8099 [--token secret]
"""
import argparse
import json
import pathlib
import re
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs

ROOT = pathlib.Path(__file__).resolve().parent.parent
HEADER = ROOT / "firmware" / "src" / "dashboard_html.h"

# Mirrors TUNABLE_FIELDS in firmware/lib/motion/tunables.cpp (key, min, max).
TUNE = {
    "onset": (0.3, 20.0), "end": (0.1, 20.0), "end_hold_ms": (0.0, 500.0),
    "post_peak_ms": (10.0, 1000.0), "max_ms": (50.0, 3000.0),
    "cooldown_ms": (0.0, 5000.0), "min_integral": (0.05, 6.0),
    "dominance": (1.0, 10.0), "min_peak": (0.5, 30.0), "lob_up_accel": (0.0, 60.0),
    "serve_pitch": (0.0, 89.0), "yaw_sign": (-1.0, 1.0), "max_tilt": (10.0, 89.0),
    "defensive_peak": (0.5, 30.0), "defensive_ms": (20.0, 1000.0),
    "loft_up_accel": (0.0, 60.0), "pull_both": (0.0, 1.0),
}
DEFAULTS = {
    "onset": 2.5, "end": 1.2, "end_hold_ms": 40.0, "post_peak_ms": 150.0,
    "max_ms": 500.0, "cooldown_ms": 400.0, "min_integral": 0.5, "dominance": 1.3,
    "min_peak": 4.0, "lob_up_accel": 6.0, "serve_pitch": 40.0, "yaw_sign": 1.0,
    "max_tilt": 60.0, "defensive_peak": 6.0, "defensive_ms": 180.0,
    "loft_up_accel": 10.0, "pull_both": 1.0,
}

STATE = {
    "sport": "tennis", "click": True, "dirty": False,
    "values": dict(DEFAULTS),
    "streaming": True, "ble": False, "calibrating": False, "error": False,
}


def load_html() -> str:
    src = HEADER.read_text()
    m = re.search(r'R"HTML\((.*)\)HTML";', src, re.S)
    if not m:
        sys.exit(f"could not extract the page from {HEADER}")
    return m.group(1)


def cross_field_error(v):
    """The same whole-snapshot rules as validate() in tunables.cpp."""
    if not (v["end"] < v["onset"]):
        return "settings combination rejected"
    if not (v["post_peak_ms"] < v["max_ms"]):
        return "settings combination rejected"
    if not (v["serve_pitch"] < v["max_tilt"]):
        return "settings combination rejected"
    if abs(abs(v["yaw_sign"]) - 1.0) > 1e-6:
        return "settings combination rejected"
    return None


def apply_form(body: str, token_ok: bool):
    """Returns (status, payload). Mirrors handleApi() for POST /api/settings."""
    if not token_ok:
        return 401, {"ok": False, "error": "unauthorized"}
    fields = parse_qs(body, keep_blank_values=True)
    cand = dict(STATE["values"])
    sport = STATE["sport"]
    click = STATE["click"]
    for key, vals in fields.items():
        val = vals[0]
        if key == "sport":
            if val not in ("tennis", "cricket"):
                return 422, {"ok": False, "error": "sport must be tennis or cricket"}
            sport = val
        elif key == "click":
            if val in ("1", "on", "true"):
                click = True
            elif val in ("0", "off", "false"):
                click = False
            else:
                return 422, {"ok": False, "error": "click must be on or off"}
        elif key in TUNE:
            try:
                f = float(val)
            except ValueError:
                return 400, {"ok": False, "error": "value is not a number"}
            lo, hi = TUNE[key]
            if not (lo <= f <= hi):
                return 422, {"ok": False, "error": "out of range"}
            cand[key] = f
        else:
            return 400, {"ok": False, "error": "unknown key"}
    err = cross_field_error(cand)
    if err:
        return 422, {"ok": False, "error": err}
    STATE.update(sport=sport, click=click, values=cand, dirty=True)
    return 200, {"ok": True}


class Handler(BaseHTTPRequestHandler):
    page = ""
    token = ""

    def log_message(self, *a):  # keep the console quiet during a browser run
        pass

    def _send(self, code, ctype, body: bytes):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _json(self, code, obj):
        self._send(code, "application/json", json.dumps(obj).encode())

    def _token_ok(self):
        if not self.token:
            return True
        hdr = self.headers.get("Authorization", "")
        return hdr == "Bearer " + self.token

    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/":
            return self._send(200, "text/html; charset=utf-8", self.page.encode())
        if path == "/api/status":
            return self._json(200, {
                "streaming": STATE["streaming"], "ble": STATE["ble"],
                "calibrating": STATE["calibrating"], "error": STATE["error"],
                "sport": STATE["sport"], "click": STATE["click"],
                "datagrams": 1234, "samples": 1200, "malformed": 0,
                "queueDrops": 0, "staleDropped": 2, "pps": 50.0, "sps": 50.0,
                "lastGesture": "forehand", "lastReason": "",
                "savedDirty": STATE["dirty"], "clickEnabled": STATE["click"],
            })
        if path == "/api/settings":
            return self._json(200, {
                "sport": STATE["sport"], "click": STATE["click"],
                "savedDirty": STATE["dirty"], "values": STATE["values"],
            })
        return self._json(404, {"ok": False, "error": "not found"})

    def do_POST(self):
        path = self.path.split("?")[0]
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length).decode("utf-8", "replace") if length else ""
        ok = self._token_ok()
        if path == "/api/settings":
            code, payload = apply_form(body, ok)
            return self._json(code, payload)
        if path == "/api/defaults":
            if not ok:
                return self._json(401, {"ok": False, "error": "unauthorized"})
            STATE.update(sport="tennis", click=True, values=dict(DEFAULTS), dirty=True)
            return self._json(200, {"ok": True})
        if path == "/api/save":
            if not ok:
                return self._json(401, {"ok": False, "error": "unauthorized"})
            STATE["dirty"] = False
            return self._json(200, {"ok": True})
        return self._json(404, {"ok": False, "error": "not found"})


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=8099)
    ap.add_argument("--token", default="", help="require this bearer token for writes")
    args = ap.parse_args()

    Handler.page = load_html()
    Handler.token = args.token
    srv = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    print(f"mock dashboard on http://127.0.0.1:{args.port}/  (token: "
          f"{'required' if args.token else 'disabled'})")
    print("this serves the firmware's real HTML; the API is a Python mirror")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()