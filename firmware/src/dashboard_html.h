// dashboard_html.h - the dashboard page, embedded so the device needs no
// filesystem. tools/dashboard_mock.py extracts this same string and serves it,
// so what is verified in a browser is byte-for-byte what the ESP32 serves.
#pragma once

constexpr char DASHBOARD_HTML[] = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MotionCtl</title>
<style>
:root{--bg:#12141a;--card:#1b1f28;--ink:#e8ebf2;--dim:#8a93a6;--ok:#3ecf8e;--bad:#ef5350;--warn:#f5a623;--accent:#4f8cff;--line:#2a3040}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.5 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;padding:16px;max-width:1100px;margin:0 auto}
h1{font-size:19px;margin:0 0 2px;letter-spacing:.02em}
.sub{color:var(--dim);font-size:13px;margin-bottom:16px}
.grid{display:grid;gap:12px;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));margin-bottom:16px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:12px 14px}
.card .k{color:var(--dim);font-size:12px;text-transform:uppercase;letter-spacing:.06em}
.card .v{font-size:20px;font-weight:600;margin-top:2px}
.ok{color:var(--ok)}.bad{color:var(--bad)}.warn{color:var(--warn)}
section{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:14px;margin-bottom:16px}
h2{font-size:13px;margin:0 0 12px;color:var(--dim);text-transform:uppercase;letter-spacing:.06em}
.fields{display:grid;gap:10px;grid-template-columns:repeat(auto-fit,minmax(170px,1fr))}
label{display:flex;flex-direction:column;gap:4px;font-size:13px;color:var(--dim)}
input,select{background:#0e1117;color:var(--ink);border:1px solid var(--line);border-radius:7px;padding:8px 10px;font:inherit;font-size:14px;width:100%}
input:focus,select:focus{outline:2px solid var(--accent);outline-offset:1px}
.row{display:flex;gap:10px;flex-wrap:wrap;margin-top:14px;align-items:center}
button{background:var(--accent);color:#fff;border:0;border-radius:8px;padding:9px 16px;font:inherit;font-weight:600;cursor:pointer}
button.ghost{background:transparent;border:1px solid var(--line);color:var(--ink)}
button:hover{filter:brightness(1.1)}
button:disabled{opacity:.5;cursor:not-allowed}
#msg{margin-top:12px;font-size:14px;min-height:20px}
#msg.err{color:var(--bad)}
#msg.good{color:var(--ok)}
.tag{display:inline-block;font-size:11px;padding:2px 7px;border-radius:99px;border:1px solid var(--line);color:var(--dim)}
/* A class-level display rule outranks the browser's [hidden] default, so the
   hidden attribute must be re-asserted or the tag never disappears. */
.tag[hidden]{display:none}
.tag.dirty{border-color:var(--warn);color:var(--warn)}
@media(max-width:560px){body{padding:12px}.card .v{font-size:17px}h1{font-size:17px}}
</style>
</head>
<body>
<h1>MotionCtl</h1>
<div class="sub">Phone motion controller &middot; <span id="sportLabel">-</span></div>

<div class="grid">
  <div class="card"><div class="k">Phone</div><div class="v" id="vStream">-</div></div>
  <div class="card"><div class="k">Bluetooth</div><div class="v" id="vBle">-</div></div>
  <div class="card"><div class="k">Calibration</div><div class="v" id="vCal">-</div></div>
  <div class="card"><div class="k">Samples/s</div><div class="v" id="vSps">-</div></div>
  <div class="card"><div class="k">Dropped</div><div class="v" id="vDrop">-</div></div>
  <div class="card"><div class="k">Last swing</div><div class="v" id="vLast" style="font-size:15px">-</div></div>
</div>

<section>
  <h2>Settings <span class="tag" id="dirtyTag" hidden>unsaved</span></h2>
  <div class="fields">
    <label>Sport
      <select id="f_sport">
        <option value="tennis">Tennis</option>
        <option value="cricket">Cricket</option>
      </select>
    </label>
    <label>Swing click
      <select id="f_click">
        <option value="1">On</option>
        <option value="0">Off</option>
      </select>
    </label>
    <label>Device token
      <input id="f_token" type="password" placeholder="write token" autocomplete="current-password">
    </label>
  </div>
  <div class="fields" id="tunableFields"></div>
  <div class="row">
    <button id="btnApply">Apply</button>
    <button id="btnSave" class="ghost">Save to device</button>
    <button id="btnDefaults" class="ghost">Restore defaults</button>
  </div>
  <div id="msg"></div>
</section>

<script>
const TUNE_KEYS = [["onset","Onset (rad/s)"],["end","End (rad/s)"],
  ["end_hold_ms","End hold (ms)"],["post_peak_ms","Post-peak (ms)"],
  ["max_ms","Max capture (ms)"],["cooldown_ms","Cooldown (ms)"],
  ["min_integral","Min rotation (rad)"],["dominance","Dominance ratio"],
  ["min_peak","Min peak (rad/s)"],["lob_up_accel","Lob up-accel (m/s²)"],
  ["serve_pitch","Serve pitch (deg)"],["yaw_sign","Handedness (1/-1)"],
  ["max_tilt","Max tilt (deg)"],["defensive_peak","Defensive peak (rad/s)"],
  ["defensive_ms","Defensive max (ms)"],["loft_up_accel","Loft up-accel (m/s²)"],
  ["pull_both","Pull both sides (0/1)"]];
const $ = id => document.getElementById(id);
let dirty = false;

// Build the numeric inputs once; their keys come from the same table the
// firmware uses, so a new tunable needs no change here.
const fields = $("tunableFields");
TUNE_KEYS.forEach(([key, label]) => {
  const l = document.createElement("label");
  l.textContent = label;
  const i = document.createElement("input");
  i.type = "number"; i.step = "any"; i.id = "f_" + key;
  i.addEventListener("input", () => { dirty = true; renderDirty(); });
  l.appendChild(i); fields.appendChild(l);
});

function renderDirty() {
  $("dirtyTag").hidden = !dirty;
}
function say(text, cls) {
  const m = $("msg"); m.textContent = text || "";
  m.className = cls || "";
}
function tokenHeaders(extra) {
  const h = Object.assign({}, extra || {});
  const t = $("f_token").value.trim();
  if (t) h["Authorization"] = "Bearer " + t;
  return h;
}

async function refresh() {
  try {
    const r = await fetch("/api/status", { cache: "no-store" });
    if (!r.ok) throw new Error("status " + r.status);
    const s = await r.json();
    $("vStream").textContent = s.streaming ? "streaming" : "no data";
    $("vStream").className = "v " + (s.streaming ? "ok" : "bad");
    $("vBle").textContent = s.ble ? "connected" : "advertising";
    $("vBle").className = "v " + (s.ble ? "ok" : "warn");
    $("vCal").textContent = s.error ? "error" : (s.calibrating ? "calibrating" : "ready");
    $("vCal").className = "v " + (s.error ? "bad" : (s.calibrating ? "warn" : "ok"));
    $("vSps").textContent = (s.sps || 0).toFixed(0);
    $("vDrop").textContent = (s.queueDrops || 0) + (s.staleDropped || 0);
    $("vLast").textContent = s.lastGesture ? s.lastGesture : "none";
    $("sportLabel").textContent = s.sport + " \u00b7 click " + (s.click ? "on" : "off");
    if (!dirty && s.savedDirty === false) renderDirty();
  } catch (e) {
    $("vStream").textContent = "offline";
    $("vStream").className = "v bad";
  }
}

async function loadSettings() {
  try {
    const r = await fetch("/api/settings", { cache: "no-store" });
    const s = await r.json();
    $("f_sport").value = s.sport;
    $("f_click").value = s.click ? "1" : "0";
    TUNE_KEYS.forEach(([key]) => {
      if (s.values && key in s.values) $("f_" + key).value = s.values[key];
    });
    dirty = false; renderDirty();
  } catch (e) { say("could not load settings: " + e.message, "err"); }
}

function formBody() {
  const parts = ["sport=" + encodeURIComponent($("f_sport").value),
                 "click=" + encodeURIComponent($("f_click").value)];
  TUNE_KEYS.forEach(([key]) => {
    const v = $("f_" + key).value;
    if (v !== "") parts.push(encodeURIComponent(key) + "=" + encodeURIComponent(v));
  });
  return parts.join("&");
}

async function apply() {
  say("applying\u2026");
  try {
    const r = await fetch("/api/settings", {
      method: "POST",
      headers: tokenHeaders({ "Content-Type": "application/x-www-form-urlencoded" }),
      body: formBody()
    });
    const j = await r.json().catch(() => ({}));
    if (!r.ok) { say(j.error || ("rejected (" + r.status + ")"), "err"); return false; }
    dirty = false; renderDirty();
    say("applied \u2014 not saved yet", "good");
    await refresh();
    return true;
  } catch (e) { say("apply failed: " + e.message, "err"); return false; }
}

async function save() {
  // Saving while the form has unapplied edits would store something other than
  // what the user is looking at, so require an apply first.
  if (dirty) { say("apply your changes first, then save", "err"); return; }
  try {
    const r = await fetch("/api/save", { method: "POST", headers: tokenHeaders() });
    const j = await r.json().catch(() => ({}));
    if (!r.ok) { say(j.error || ("save failed (" + r.status + ")"), "err"); return; }
    say("saved to device", "good");
  } catch (e) { say("save failed: " + e.message, "err"); }
}

async function defaults() {
  try {
    const r = await fetch("/api/defaults", { method: "POST", headers: tokenHeaders() });
    const j = await r.json().catch(() => ({}));
    if (!r.ok) { say(j.error || ("could not restore defaults (" + r.status + ")"), "err"); return; }
    await loadSettings();
    dirty = true; renderDirty();
    say("defaults loaded \u2014 press Apply, then Save", "good");
  } catch (e) { say("defaults failed: " + e.message, "err"); }
}

$("btnApply").addEventListener("click", apply);
$("btnSave").addEventListener("click", save);
$("btnDefaults").addEventListener("click", defaults);

loadSettings();
refresh();
setInterval(refresh, 1000);
</script>
</body>
</html>
)HTML";