// Phases 2-6: CSV parse -> counters/watchdog -> calibration -> gravity filter
// -> swing detection (tennis or cricket) -> either a BLE HID gamepad (tilt on
// the stick) or a BLE HID mouse (gyro cursor + one click per recognised
// stroke), chosen by OUTPUT_MODE in config.h.
//
// Threading: the process task is the SINGLE OWNER of configuration and output
// state. The serial and button tasks only submit Commands through a bounded
// queue, so nothing writes settings while a swing is being scored. The safety
// rule (no valid data for DATA_TIMEOUT_MS) forces neutral outputs and stays
// neutral until fresh data arrives.
#include <Arduino.h>
#include <IPAddress.h>

#include <atomic>
#include <stdarg.h>
#include <string.h>

#include "config.h"
#include "controller.h"
#include "dashboard.h"
#include "debounce.h"
#include "gamepad_ble.h"
#include "gestures.h"
#include "gyro_mouse.h"
#include "led_pattern.h"
#include "log_ring.h"
#include "mouse_ble.h"
#include "net_udp.h"
#include "output_gate.h"
#include "packet_stats.h"
#include "parser.h"
#include "pipeline.h"
#include "settings_nvs.h"
#include "stick.h"
#include "tunables.h"

namespace {

// Critical section for the diagnostic ring (producer and consumer are on
// different tasks, so the ring's indices need guarding - see log_ring.h).
portMUX_TYPE s_logMux = portMUX_INITIALIZER_UNLOCKED;
struct EspLogLock {
    static void enter() { portENTER_CRITICAL(&s_logMux); }
    static void exit() { portEXIT_CRITICAL(&s_logMux); }
};

QueueHandle_t s_rawQueue = nullptr;
QueueHandle_t s_cmdQueue = nullptr;

const CsvLayout kLayout = CsvLayout::fromConfig();
PacketStats s_stats;
OrientationPipeline s_pipeline;
OutputGate s_gate;
GamepadBle s_pad;
MouseBle s_mouse;
GyroMouse s_gyroMouse;
GestureDetector s_gestures;
Controller s_control;
SwingFeatures s_swing;
PipelineOutput s_out;
bool s_outputWasConnected = false;
bool s_fatalError = false;      // AP/UDP startup failure -> LED error state

// Forward declaration: publishSnapshot() needs this before its definition.
bool outputConnected();

// Bounded diagnostics queue: the process task never blocks on serial output.
LogRing<48, 192, EspLogLock> s_logRing;
uint32_t s_swingCount = 0;
uint32_t s_clickCount = 0;
uint32_t s_staleDropped = 0;

// ---- snapshot the dashboard reads, published by the process task only
SemaphoreHandle_t s_snapMux = nullptr;
StatusSnapshot s_publishedStatus;
SavedSettings s_publishedSettings;
bool s_savedDirty = true;
char s_lastGestureName[24] = "";
char s_lastRejectReason[24] = "";
SettingsNvs s_nvs;

void publishSnapshot(uint32_t nowMs) {
    if (!s_snapMux) return;
    StatusSnapshot st;
    st.phoneStreaming = !s_stats.isStale(nowMs, DATA_TIMEOUT_MS);
    st.bleConnected = outputConnected();
    st.calibrating = s_out.calibrating;
    st.error = s_fatalError;
    st.sport = s_control.state().sport;
    st.clickEnabled = s_control.state().swingOutputEnabled;
    st.datagrams = s_stats.datagrams();
    st.samples = s_stats.samples();
    st.malformed = s_stats.malformed();
    st.queueDrops = s_stats.queueDrops();
    st.staleDropped = s_staleDropped;
    st.packetsPerSec = s_stats.packetsPerSec();
    st.samplesPerSec = s_stats.samplesPerSec();
    st.lastGesture = s_lastGestureName;
    st.lastReason = s_lastRejectReason;

    SavedSettings set;
    set.sport = s_control.state().sport;
    set.swingOutputEnabled = s_control.state().swingOutputEnabled;
    set.tunables = s_control.state().tunables;

    if (xSemaphoreTake(s_snapMux, pdMS_TO_TICKS(5)) == pdTRUE) {
        s_publishedStatus = st;
        s_publishedSettings = set;
        xSemaphoreGive(s_snapMux);
    }
}

// Copy the published snapshot for the web task to use.
void readSnapshot(StatusSnapshot &status, SavedSettings &settings, bool &dirty) {
    if (!s_snapMux) return;
    if (xSemaphoreTake(s_snapMux, pdMS_TO_TICKS(20)) == pdTRUE) {
        status = s_publishedStatus;
        settings = s_publishedSettings;
        dirty = s_savedDirty;
        xSemaphoreGive(s_snapMux);
    }
}

bool outputConnected() {
    return OUTPUT_MODE == OutputMode::Gamepad ? s_pad.connected() : s_mouse.connected();
}

std::atomic<bool> s_rawDump{DUMP_RAW};
std::atomic<bool> s_hexDump{DUMP_HEX};
std::atomic<bool> s_angles{PRINT_ANGLES};
std::atomic<bool> s_tuning{false};   // per-sample CSV records for the tools
std::atomic<bool> s_statsRequest{false};
std::atomic<bool> s_tuneDumpRequest{false};
std::atomic<bool> s_recalRequest{false};

// Queue one diagnostic line for the serial task (never blocks).
void logLine(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void logLine(const char *fmt, ...) {
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    s_logRing.push(buf);  // dropped and counted when full
}

// Print a payload as text, escaping non-printable bytes so binary formats are
// still readable and a stray control char can't mess up the terminal.
void printEscaped(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        const uint8_t c = data[i];
        if (c == '\n')       Serial.print("\\n");
        else if (c == '\r')  Serial.print("\\r");
        else if (c >= 0x20 && c < 0x7F) Serial.write(c);
        else                 Serial.printf("\\x%02X", c);
    }
}

void printHex(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) Serial.printf("%02X ", data[i]);
}

void printStats(uint32_t nowMs) {
    const uint32_t since = s_stats.msSinceLastValid(nowMs);
    char sinceBuf[24];
    if (since == UINT32_MAX) snprintf(sinceBuf, sizeof(sinceBuf), "never");
    else snprintf(sinceBuf, sizeof(sinceBuf), "%lu ms", static_cast<unsigned long>(since));

    Serial.printf("[stats] datagrams=%lu samples=%lu malformed=%lu overflow=%lu "
                  "truncated=%lu qdrop=%lu stale=%lu swings=%lu clicks=%lu "
                  "pps=%.1f sps=%.1f since_valid=%s ble=%s sport=%s click=%s\n",
                  static_cast<unsigned long>(s_stats.datagrams()),
                  static_cast<unsigned long>(s_stats.samples()),
                  static_cast<unsigned long>(s_stats.malformed()),
                  static_cast<unsigned long>(s_stats.overflow()),
                  static_cast<unsigned long>(s_stats.truncated()),
                  static_cast<unsigned long>(s_stats.queueDrops()),
                  static_cast<unsigned long>(s_staleDropped),
                  static_cast<unsigned long>(s_swingCount),
                  static_cast<unsigned long>(s_clickCount),
                  s_stats.packetsPerSec(), s_stats.samplesPerSec(), sinceBuf,
                  outputConnected() ? "connected" : "advertising",
                  sportName(s_control.state().sport),
                  s_control.state().swingOutputEnabled ? "on" : "off");
}

void printAngles() {
    if (s_out.calibrating) {
        Serial.printf("[cal] collecting %.0f%% (failed=%lu last=%s)\n", s_out.calProgress * 100.0f,
                      static_cast<unsigned long>(s_pipeline.calFailures()),
                      s_pipeline.lastCalFailure());
    } else if (s_out.anglesValid) {
        Serial.printf("[angles] roll=%+.1f pitch=%+.1f deg\n", s_out.rollDeg, s_out.pitchDeg);
    } else {
        Serial.println("[angles] unusable: pitch near 90 deg");
    }
}

void printCommandHelp() {
    Serial.println("[cmd] recal | raw | hex | angles | stats | tune on/off | get | "
                   "set <key> <value> | defaults | sport tennis|cricket | mode | click on|off");
}

// Submit a command to the owner (process task). Non-blocking: a burst of
// commands reports "busy" rather than stalling the serial line.
bool submit(const Command &cmd) {
    if (xQueueSend(s_cmdQueue, &cmd, 0) != pdTRUE) {
        Serial.println("[cmd] busy: too many commands queued");
        return false;
    }
    return true;
}

// `set <key> <value>` - the owner validates both the field and the resulting
// snapshot; this only parses.
void handleSet(char *rest) {
    char *key = strtok(rest, " \t");
    char *val = strtok(nullptr, " \t");
    if (!key || !val) {
        Serial.println("[cmd] usage: set <key> <value>   (see `get`)");
        return;
    }
    char *end = nullptr;
    const float v = strtof(val, &end);
    if (end == val || (end && *end != '\0')) {
        Serial.printf("[cmd] not a number: %s\n", val);
        return;
    }
    if (submit(cmdSetTunable(key, v))) {
        // The owner logs the result (it knows whether the snapshot survived).
    }
}

// Reads one line at a time from the USB serial port (diagnostic commands).
void serialTask(void *) {
    char line[SERIAL_CMD_MAX_LEN];
    char drained[192];
    uint32_t lastDropReport = 0;
    size_t len = 0;
    for (;;) {
        while (Serial.available() > 0) {
            const char c = static_cast<char>(Serial.read());
            if (c == '\n' || c == '\r') {
                line[len] = '\0';
                if (len > 0) {
                    if (!strcmp(line, "recal")) {
                        submit(cmdRecalibrate());
                    } else if (!strcmp(line, "raw")) {
                        const bool next = !s_rawDump;
                        s_rawDump = next;
                        Serial.printf("[cmd] raw %s\n", next ? "on" : "off");
                    } else if (!strcmp(line, "hex")) {
                        const bool next = !s_hexDump;
                        s_hexDump = next;
                        Serial.printf("[cmd] hex %s\n", next ? "on" : "off");
                    } else if (!strcmp(line, "angles")) {
                        const bool next = !s_angles;
                        s_angles = next;
                        Serial.printf("[cmd] angles %s\n", next ? "on" : "off");
                    } else if (!strcmp(line, "stats")) {
                        submit(cmdRequestStats());
                    } else if (!strcmp(line, "tune") || !strcmp(line, "tune on")) {
                        s_tuning = true;
                        Serial.println("[cmd] tune on (CSV records; `tune off` to stop)");
                    } else if (!strcmp(line, "tune off")) {
                        s_tuning = false;
                        Serial.println("[cmd] tune off");
                    } else if (!strcmp(line, "get")) {
                        submit(cmdRequestTunables());
                    } else if (!strcmp(line, "defaults")) {
                        submit(cmdResetTunables());
                    } else if (!strncmp(line, "set ", 4)) {
                        handleSet(line + 4);
                    } else if (!strcmp(line, "mode")) {
                        submit(cmdToggleSport());
                    } else if (!strcmp(line, "sport tennis")) {
                        submit(cmdSetSport(Sport::Tennis));
                    } else if (!strcmp(line, "sport cricket")) {
                        submit(cmdSetSport(Sport::Cricket));
                    } else if (!strcmp(line, "click")) {
                        submit(cmdToggleSwingOutput());
                    } else if (!strcmp(line, "click on")) {
                        submit(cmdSetSwingOutput(true));
                    } else if (!strcmp(line, "click off")) {
                        submit(cmdSetSwingOutput(false));
                    } else {
                        printCommandHelp();
                    }
                }
                len = 0;
            } else if (len + 1 < sizeof(line)) {
                if (c >= 0x20 && c < 0x7F) line[len++] = c;
            }
        }

        // Drain queued diagnostics. Non-blocking: the ring drops instead, and
        // the loss is reported periodically rather than silently.
        while (s_logRing.pop(drained)) Serial.println(drained);
        const uint32_t dropped = s_logRing.takeDropped();
        if (dropped) {
            Serial.printf("[log] %lu record(s) dropped (output slower than events)\n",
                          static_cast<unsigned long>(dropped));
        } else {
            (void)lastDropReport;
        }

        vTaskDelay(pdMS_TO_TICKS(PROCESS_WAKE_MS));
    }
}

// --------------------------------------------------------------- buttons
// Active-low: pressed reads LOW through the internal pull-up.
struct ButtonPoll {
    uint8_t    pin;
    Debouncer  deb;
    uint32_t   lastEventMs = 0;
    bool       pending = false;   // an edge was seen; fire once past the gap
};

ButtonPoll s_btnStart{PIN_BTN_START, Debouncer(BTN_DEBOUNCE_MS)};
ButtonPoll s_btnRecal{PIN_BTN_RECAL, Debouncer(BTN_DEBOUNCE_MS)};
ButtonPoll s_btnMode{PIN_BTN_MODE, Debouncer(BTN_DEBOUNCE_MS)};

// Returns true if this button produced a press (not a release) now.
bool pollPress(ButtonPoll &b, uint32_t nowMs) {
    const bool raw = digitalRead(b.pin) == LOW;
    const bool changed = b.deb.update(raw, nowMs);
    if (!changed || !b.deb.pressed()) return false;
    // Debounce double-tap: ignore a second press inside BTN_MIN_GAP_MS.
    if (nowMs - b.lastEventMs < BTN_MIN_GAP_MS) return false;
    b.lastEventMs = nowMs;
    return true;
}

void buttonTask(void *) {
    for (;;) {
        const uint32_t now = millis();
        if (pollPress(s_btnMode, now)) submit(cmdToggleSport());
        if (pollPress(s_btnRecal, now)) submit(cmdRecalibrate());
        if (pollPress(s_btnStart, now)) submit(cmdToggleSwingOutput());
        vTaskDelay(pdMS_TO_TICKS(PROCESS_WAKE_MS));
    }
}

// Called once per finished swing (recognised or not). Runs on the process task.
void onSwingEnd(const SwingFeatures &f, uint32_t nowMs) {
    s_swingCount++;
    const bool hit = f.gesture != Gesture::None;
    const MouseSettings ms = MouseSettings::fromConfig();

    // Remember the verdict for the dashboard.
    snprintf(s_lastGestureName, sizeof(s_lastGestureName), "%s", gestureName(f.gesture));
    snprintf(s_lastRejectReason, sizeof(s_lastRejectReason), "%s", f.rejectReason);

    if (s_tuning) {
        // Machine-readable record for tools/serial_logger.py (see docs/testing.md).
        logLine("W,%lu,%lu,%.2f,%.2f,%.2f,%.3f,%.3f,%.3f,%.1f,%s,%s",
                static_cast<unsigned long>(f.tMs), static_cast<unsigned long>(f.durationMs),
                f.peakGyro, f.peakLinAccel, f.peakUpAccel, f.intYaw, f.intPitch, f.intRoll,
                f.onsetPitchDeg, gestureName(f.gesture), f.rejectReason);
    } else if (hit) {
        logLine("[swing] %s %s peak=%.1f rad/s dur=%lu ms yaw=%.2f pitch=%.2f",
                sportName(s_control.state().sport), gestureName(f.gesture), f.peakGyro,
                static_cast<unsigned long>(f.durationMs), f.intYaw, f.intPitch);
    } else {
        logLine("[swing] %s rejected (%s) peak=%.1f rad/s", sportName(s_control.state().sport),
                f.rejectReason, f.peakGyro);
    }

    // One bounded click per recognised stroke, and only when the user has left
    // swing output enabled.
    if (hit && s_control.state().swingOutputEnabled && OUTPUT_MODE == OutputMode::Mouse) {
        s_gyroMouse.triggerClick(nowMs, ms);  // ignored if a pulse is still active
        if (s_gyroMouse.pulseActive()) s_clickCount++;
    }
}

// Apply one command from the owner queue, with its side effects. Runs on the
// process task, which is why nothing here needs a lock.
void applyCommand(const Command &cmd, uint32_t nowMs) {
    if (Controller::invalidatesSwing(cmd.kind)) {
        // Settings, sport or output changed underneath any capture in progress:
        // abandon it and cancel a pending click so nothing half-classified or
        // stale is emitted.
        s_gestures.reset();
        s_gyroMouse.clearClick();
    }

    switch (cmd.kind) {
        case CommandKind::Recalibrate:
            s_pipeline.requestRecalibration();
            Serial.println("[cal] recalibrating: hold the phone flat and still");
            return;

        case CommandKind::RequestStats:
            printStats(nowMs);
            return;

        case CommandKind::RequestTunables:
            tunablesDump(s_control.state().tunables, [](const char *line) {
                logLine("T,%s", line);
            });
            logLine("T,sport=%s", sportName(s_control.state().sport));
            logLine("T,click=%d", s_control.state().swingOutputEnabled ? 1 : 0);
            return;

        case CommandKind::SaveSettings: {
            SavedSettings toSave;
            toSave.sport = s_control.state().sport;
            toSave.swingOutputEnabled = s_control.state().swingOutputEnabled;
            toSave.tunables = s_control.state().tunables;
            if (s_nvs.save(toSave)) {
                s_savedDirty = false;
                logLine("[save] settings saved");
            } else {
                logLine("[save] FAILED - settings not persisted");
            }
            return;
        }

        default:
            break;
    }

    const ControlState before = s_control.state();
    const CommandResult r = s_control.apply(cmd);
    const ControlState &now = s_control.state();

    switch (r) {
        case CommandResult::Ok:
            break;
        case CommandResult::NoChange:
            logLine("[cmd] %s: %s", commandResultText(r), cmd.key[0] ? cmd.key : "unchanged");
            return;
        case CommandResult::InvalidKey:
        case CommandResult::InvalidValue:
        case CommandResult::InvalidSnapshot:
            logLine("[cmd] rejected %s%s%s", cmd.key[0] ? cmd.key : "",
                    cmd.key[0] ? ": " : "", commandResultText(r));
            return;
    }

    // Report what actually changed. Any accepted configuration change means the
    // live settings no longer match what is stored, so the dashboard shows
    // "unsaved" until an explicit save.
    if (r == CommandResult::Ok) s_savedDirty = true;
    if (now.sport != before.sport) {
        s_gestures.setSport(now.sport);
        logLine("[mode] %s", sportName(now.sport));
    }
    if (now.swingOutputEnabled != before.swingOutputEnabled) {
        logLine("[click] swing output %s", now.swingOutputEnabled ? "on" : "off");
    }
    if (cmd.kind == CommandKind::SetTunable) {
        float v = 0.0f;
        if (tunablesGet(now.tunables, cmd.key, v)) {
            logLine("[cmd] %s=%g", cmd.key, static_cast<double>(v));
        }
    } else if (cmd.kind == CommandKind::ResetTunables) {
        logLine("[cmd] tunables reset to config.h defaults");
    }
}

void drainCommands(uint32_t nowMs) {
    Command cmd;
    // Bounded per pass so a flood cannot starve packet processing.
    for (int i = 0; i < 8 && xQueueReceive(s_cmdQueue, &cmd, 0) == pdTRUE; i++) {
        applyCommand(cmd, nowMs);
    }
}

// LED reflects the firmware's real condition, with Error winning over all.
void updateLed(uint32_t nowMs) {
    static uint32_t modeChangedMs = 0;
    static Sport lastSport = Sport::Tennis;
    static bool primed = false;

    const Sport sport = s_control.state().sport;
    if (!primed || sport != lastSport) {
        primed = true;
        lastSport = sport;
        modeChangedMs = nowMs;
    }

    LedInputs in;
    in.error = s_fatalError;
    in.calibrating = s_out.calibrating;
    in.phoneStreaming = !s_stats.isStale(nowMs, DATA_TIMEOUT_MS);
    in.bleConnected = outputConnected();
    const LedState state = ledStateFor(in);

    bool level;
    if (state == LedState::Error) {
        level = ledPatternLevel(LedState::Error, nowMs);  // overlay never hides an error
    } else if (ledModeOverlay(sport, nowMs - modeChangedMs, level)) {
        // mode flash owns the LED briefly
    } else {
        level = ledPatternLevel(state, nowMs);
    }
    digitalWrite(PIN_LED, level ? HIGH : LOW);
}

void processSample(const ImuSample &s) {
    s_stats.onValidSample(s.rxMs);
    s_pipeline.feed(s, s_out);

    // Records include the calibration window as well as the running phase, so
    // tools/replay_swings.cpp can reproduce the same timeline instead of
    // burning its own calibration on the first samples it sees.
    if (s_tuning) {
        char row[208];
        snprintf(row, sizeof(row),
                 "S,%lu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f,%.1f,%.3f,%d",
                 static_cast<unsigned long>(s.rxMs), s.accel.x, s.accel.y, s.accel.z,
                 s.gyro.x, s.gyro.y, s.gyro.z, s_out.up.x, s_out.up.y, s_out.up.z,
                 s_out.rollDeg, s_out.pitchDeg, s_out.dtS, s_out.calibrating ? 1 : 0);
        s_logRing.push(row);
    }

    // ------------------------------------------------------- gesture detection
    if (!s_out.calibrating) {
        // A real silence means motion either side cannot be integrated as one
        // swing, so drop any partial capture before using this sample.
        if (s_out.timingGap) s_gestures.reset();

        GestureInput in;
        in.gyro = s_out.gyro;
        in.accel = s.accel;
        in.up = s_out.up;
        in.gravityMag = s_out.gravityMag;
        in.pitchDeg = s_out.pitchDeg;
        in.rollDeg = s_out.rollDeg;
        in.dt = s_out.dtS;
        in.nowMs = s.rxMs;  // wrap-safe: the detector compares with signed deltas

        if (s_gestures.update(in, s_control.state().tunables, s_swing)) {
            onSwingEnd(s_swing, s.rxMs);
        }
    } else {
        s_gestures.reset();
    }

    // ---------------------------------------------------------------- outputs
    if (OUTPUT_MODE == OutputMode::Gamepad) {
        if (!s_out.calibrating && s_out.anglesValid) {
            const StickOut st = mapTilt(s_out.rollDeg, s_out.pitchDeg, STICK_DEADZONE_DEG,
                                        STICK_FULLSCALE_DEG, STICK_X_SIGN, STICK_Y_SIGN);
            s_pad.setStick(st.x, st.y);
        } else {
            s_pad.setStick(0.0f, 0.0f);  // calibrating or angles unusable: centred
        }
    } else {
        if (!s_out.calibrating) {
            s_gyroMouse.feed(s_out.gyro, s_out.up, s_out.dtS, MouseSettings::fromConfig());
        } else {
            s_gyroMouse.reset();  // gyro bias not known yet: keep the cursor still
        }
    }
    if (s_out.calJustCompleted) {
        const Vec3 bias = s_pipeline.calibration().gyroBias;
        logLine("[cal] done: gyro bias=(%+.3f %+.3f %+.3f) rad/s, |g|=%.2f m/s^2", bias.x, bias.y,
                bias.z, s_pipeline.calibration().gravityMag);
    }
}

// Consumer task: parse datagrams, apply commands, maintain the watchdog and
// drive the pipeline. Owns all configuration and output state.
void processTask(void *) {
    static RawPacket pkt;
    static ImuSample samples[CSV_MAX_SAMPLES_PER_PACKET];
    uint32_t lastStats = 0;
    uint32_t lastAngles = 0;
    uint32_t lastLed = 0;

    for (;;) {
        const bool gotPacket =
            xQueueReceive(s_rawQueue, &pkt, pdMS_TO_TICKS(PROCESS_WAKE_MS)) == pdTRUE;
        const uint32_t now = millis();

        drainCommands(now);

        if (gotPacket) {
            s_stats.onDatagram(now);
            s_stats.setQueueDrops(netPacketsDroppedQueueFull());

            // A packet that sat in the queue longer than the timeout must not
            // drive any output: replaying it would produce motion from stale
            // data after the safety release already fired.
            const bool packetStale = (now - pkt.rxMs) > DATA_TIMEOUT_MS;
            if (packetStale) {
                s_staleDropped++;
            } else {
                if (s_rawDump) {
                    Serial.printf("[rx %lu ms] %s:%u len=%u%s | ",
                                  static_cast<unsigned long>(pkt.rxMs),
                                  IPAddress(pkt.srcIp).toString().c_str(), pkt.srcPort, pkt.len,
                                  pkt.truncated ? " TRUNCATED" : "");
                    printEscaped(pkt.data, pkt.len);
                    Serial.println();
                    if (s_hexDump) {
                        Serial.print("    hex: ");
                        printHex(pkt.data, pkt.len);
                        Serial.println();
                    }
                }

                const PayloadResult r = parsePayload(pkt.data, pkt.len, kLayout, pkt.truncated,
                                                    samples, CSV_MAX_SAMPLES_PER_PACKET);
                s_stats.onParsed(r.samples, r.malformed, r.overflow, r.truncated);
                for (size_t i = 0; i < r.samples; i++) {
                    samples[i].rxMs = pkt.rxMs;
                    processSample(samples[i]);
                }
            }
        }

        s_stats.tick(now);

        // Safety rule: no valid sample for DATA_TIMEOUT_MS -> release inputs.
        const bool live = !s_stats.isStale(now, DATA_TIMEOUT_MS);
        switch (s_gate.update(live)) {
            case GateAction::Release:
                s_pipeline.onDataLost(now);
                s_gestures.reset();   // drop any half-captured swing
                if (OUTPUT_MODE == OutputMode::Gamepad) s_pad.releaseAll();
                else s_mouse.releaseAll(s_gyroMouse);
                s_gyroMouse.clearClick();
                logLine("[safe] no valid data for %lu ms - inputs released",
                        static_cast<unsigned long>(s_stats.msSinceLastValid(now)));
                break;
            case GateAction::Resume:
                s_gestures.reset();   // never resume into the middle of a swing
                s_gyroMouse.clearClick();
                logLine("[safe] valid data again - inputs live");
                break;
            case GateAction::None:
                break;
        }

        // While stale, hold neutral for the whole state (not just at the
        // transition) and keep the outputs cleared.
        if (!live) {
            s_gestures.reset();
            s_gyroMouse.clearClick();
            if (OUTPUT_MODE == OutputMode::Gamepad) s_pad.setStick(0.0f, 0.0f);
        }

        if (OUTPUT_MODE == OutputMode::Gamepad) s_pad.update(now);
        else s_mouse.update(now, s_gyroMouse);

        const bool connected = outputConnected();
        if (connected != s_outputWasConnected) {
            // A fresh connection starts from a clean slate: no replayed click.
            s_outputWasConnected = connected;
            if (!connected) {
                s_gyroMouse.clearClick();
                s_gestures.reset();
            }
            Serial.println(connected ? "[ble] host connected"
                                     : "[ble] host disconnected - advertising");
        }

        if (now - lastLed >= LED_UPDATE_PERIOD_MS) {
            lastLed = now;
            updateLed(now);
            publishSnapshot(now);
        }

        if (s_angles && now - lastAngles >= DEBUG_ANGLES_PERIOD_MS) {
            lastAngles = now;
            if (live) printAngles();
        }
        if (STATS_PERIOD_MS && now - lastStats >= STATS_PERIOD_MS) {
            lastStats = now;
            printStats(now);
        }
    }
}

// Web task: lowest priority, so HTTP can never outrank packet processing. It
// works from a copy of the published snapshot and hands changes back as
// Commands, keeping the process task the single owner of configuration.
void webTask(void *) {
    static Dashboard dash;
    if (!dash.begin(submit)) {
        logLine("[web] dashboard failed to start");
        vTaskDelete(nullptr);
        return;
    }
    logLine("[web] dashboard on http://%u.%u.%u.%u/", AP_IP[0], AP_IP[1], AP_IP[2], AP_IP[3]);
    for (;;) {
        StatusSnapshot status;
        SavedSettings settings;
        bool dirty = true;
        readSnapshot(status, settings, dirty);
        dash.poll(status, settings, dirty);
        vTaskDelay(pdMS_TO_TICKS(WEB_POLL_MS));
    }
}

}  // namespace

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(500);  // let the USB-serial monitor attach
    Serial.println("\n=== ESP32 phone motion controller - phases 2-6 ===");

    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);
    // Buttons: active-low with internal pull-ups.
    pinMode(PIN_BTN_START, INPUT_PULLUP);
    pinMode(PIN_BTN_RECAL, INPUT_PULLUP);
    pinMode(PIN_BTN_MODE, INPUT_PULLUP);

    s_cmdQueue = xQueueCreate(CMD_QUEUE_LEN, sizeof(Command));
    if (!s_cmdQueue) {
        Serial.println("[fatal] could not create the command queue");
        s_fatalError = true;
        return;
    }
    s_snapMux = xSemaphoreCreateMutex();
    if (!s_snapMux) {
        Serial.println("[fatal] could not create the snapshot mutex");
        s_fatalError = true;
        return;
    }
    s_control.begin();

    if (!netStartAccessPoint()) {
        Serial.println("[fatal] could not start AP");
        s_fatalError = true;
        return;
    }
    s_rawQueue = netStartUdp();
    if (!s_rawQueue) {
        Serial.println("[fatal] could not start UDP task");
        s_fatalError = true;
        return;
    }

    s_stats.reset(millis());
    s_pipeline.begin();

    // Restore saved settings before anything starts processing, so the first
    // swing is scored with the user's thresholds rather than the defaults.
    {
        SavedSettings loaded;
        const SettingsLoadStatus st = s_nvs.load(loaded);
        if (st == SettingsLoadStatus::Ok) {
            s_control.apply(cmdSetSport(loaded.sport));
            s_control.apply(cmdSetSwingOutput(loaded.swingOutputEnabled));
            for (size_t i = 0; i < TUNABLE_FIELD_COUNT; i++) {
                s_control.apply(cmdSetTunable(TUNABLE_FIELDS[i].key,
                                              loaded.tunables.*(TUNABLE_FIELDS[i].member)));
            }
            s_savedDirty = false;
            Serial.printf("[save] restored saved settings (sport=%s)\n",
                          sportName(s_control.state().sport));
        } else if (st != SettingsLoadStatus::Empty) {
            // Corrupt or incompatible record: keep defaults and say so loudly.
            Serial.printf("[save] saved settings ignored: %s\n", settingsLoadStatusText(st));
            s_savedDirty = false;
        } else {
            s_savedDirty = false;  // nothing stored yet
        }
    }

    s_gestures.setSport(s_control.state().sport);
    if (OUTPUT_MODE == OutputMode::Gamepad) {
        s_pad.begin();
        Serial.printf("[ble] advertising as \"%s\" (HID gamepad)\n", BLE_DEVICE_NAME);
    } else {
        s_mouse.begin();
        Serial.printf("[ble] advertising as \"%s\" (HID mouse)\n", BLE_MOUSE_NAME);
    }

    xTaskCreatePinnedToCore(processTask, "process", TASK_STACK_PROCESS, nullptr, TASK_PRIO_PROCESS,
                            nullptr, TASK_CORE_APP);
    xTaskCreatePinnedToCore(serialTask, "serial", TASK_STACK_SERIAL, nullptr, TASK_PRIO_SERIAL,
                            nullptr, TASK_CORE_APP);
    xTaskCreatePinnedToCore(buttonTask, "buttons", TASK_STACK_BUTTONS, nullptr, TASK_PRIO_SERIAL,
                            nullptr, TASK_CORE_APP);
    if (ENABLE_DASHBOARD) {
        xTaskCreatePinnedToCore(webTask, "web", TASK_STACK_WEB, nullptr, TASK_PRIO_WEB, nullptr,
                                TASK_CORE_APP);
    }

    Serial.printf("[cal] hold the phone flat and still for %lu ms\n",
                  static_cast<unsigned long>(CAL_DURATION_MS));
    Serial.printf("[mode] %s, swing click %s\n", sportName(s_control.state().sport),
                  s_control.state().swingOutputEnabled ? "on" : "off");
    printCommandHelp();
}

void loop() {
    vTaskDelay(portMAX_DELAY);  // all work happens in tasks
}