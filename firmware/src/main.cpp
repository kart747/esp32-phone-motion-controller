// Phases 2-4: CSV parse -> counters/watchdog -> calibration -> gravity filter
// -> roll/pitch -> analog stick on a BLE HID gamepad. The safety rule (no
// valid data for DATA_TIMEOUT_MS) centres the stick and releases all inputs.
#include <Arduino.h>
#include <IPAddress.h>

#include <atomic>
#include <string.h>

#include "config.h"
#include "gamepad_ble.h"
#include "net_udp.h"
#include "output_gate.h"
#include "packet_stats.h"
#include "parser.h"
#include "pipeline.h"
#include "stick.h"

namespace {

QueueHandle_t s_rawQueue = nullptr;

const CsvLayout kLayout = CsvLayout::fromConfig();
PacketStats s_stats;
OrientationPipeline s_pipeline;
OutputGate s_gate;
GamepadBle s_pad;
bool s_padWasConnected = false;
PipelineOutput s_out;

std::atomic<bool> s_rawDump{DUMP_RAW};
std::atomic<bool> s_hexDump{DUMP_HEX};
std::atomic<bool> s_angles{PRINT_ANGLES};
std::atomic<bool> s_recalRequest{false};
std::atomic<bool> s_statsRequest{false};

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
                  "truncated=%lu qdrop=%lu pps=%.1f sps=%.1f since_valid=%s ble=%s\n",
                  static_cast<unsigned long>(s_stats.datagrams()),
                  static_cast<unsigned long>(s_stats.samples()),
                  static_cast<unsigned long>(s_stats.malformed()),
                  static_cast<unsigned long>(s_stats.overflow()),
                  static_cast<unsigned long>(s_stats.truncated()),
                  static_cast<unsigned long>(s_stats.queueDrops()),
                  s_stats.packetsPerSec(), s_stats.samplesPerSec(), sinceBuf,
                  s_pad.connected() ? "connected" : "advertising");
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

// Reads one line at a time from the USB serial port (diagnostic commands).
void serialTask(void *) {
    char line[SERIAL_CMD_MAX_LEN];
    size_t len = 0;
    for (;;) {
        while (Serial.available() > 0) {
            const char c = static_cast<char>(Serial.read());
            if (c == '\n' || c == '\r') {
                line[len] = '\0';
                if (len > 0) {
                    if (!strcmp(line, "recal")) {
                        s_recalRequest = true;
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
                        s_statsRequest = true;
                    } else {
                        Serial.println("[cmd] recal | raw | hex | angles | stats");
                    }
                }
                len = 0;
            } else if (len + 1 < sizeof(line)) {
                if (c >= 0x20 && c < 0x7F) line[len++] = c;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(PROCESS_WAKE_MS));
    }
}

void processSample(const ImuSample &s) {
    s_stats.onValidSample(s.rxMs);
    s_pipeline.feed(s, s_out);
    if (!s_out.calibrating && s_out.anglesValid) {
        const StickOut st = mapTilt(s_out.rollDeg, s_out.pitchDeg, STICK_DEADZONE_DEG,
                                    STICK_FULLSCALE_DEG, STICK_X_SIGN, STICK_Y_SIGN);
        s_pad.setStick(st.x, st.y);
    } else {
        s_pad.setStick(0.0f, 0.0f);  // calibrating or angles unusable: centred
    }
    if (s_out.calJustCompleted) {
        const Vec3 bias = s_pipeline.calibration().gyroBias;
        Serial.printf("[cal] done: gyro bias=(%+.3f %+.3f %+.3f) rad/s, |g|=%.2f m/s^2\n",
                      bias.x, bias.y, bias.z, s_pipeline.calibration().gravityMag);
    }
}

// Consumer task: parse datagrams, maintain the watchdog, drive the pipeline.
void processTask(void *) {
    static RawPacket pkt;
    static ImuSample samples[CSV_MAX_SAMPLES_PER_PACKET];
    uint32_t lastStats = 0;
    uint32_t lastAngles = 0;

    for (;;) {
        const bool gotPacket =
            xQueueReceive(s_rawQueue, &pkt, pdMS_TO_TICKS(PROCESS_WAKE_MS)) == pdTRUE;
        const uint32_t now = millis();

        if (gotPacket) {
            s_stats.onDatagram(now);
            s_stats.setQueueDrops(netPacketsDroppedQueueFull());

            if (s_rawDump) {
                Serial.printf("[rx %lu ms] %s:%u len=%u%s | ", static_cast<unsigned long>(pkt.rxMs),
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

            const PayloadResult r = parsePayload(pkt.data, pkt.len, kLayout, pkt.truncated, samples,
                                                CSV_MAX_SAMPLES_PER_PACKET);
            s_stats.onParsed(r.samples, r.malformed, r.overflow, r.truncated);
            for (size_t i = 0; i < r.samples; i++) {
                samples[i].rxMs = pkt.rxMs;
                processSample(samples[i]);
            }
        }

        s_stats.tick(now);

        if (s_recalRequest.exchange(false)) {
            s_pipeline.requestRecalibration();
            Serial.println("[cal] recalibrating: hold the phone flat and still");
        }

        // Safety rule: no valid sample for DATA_TIMEOUT_MS -> release inputs.
        const bool live = !s_stats.isStale(now, DATA_TIMEOUT_MS);
        switch (s_gate.update(live)) {
            case GateAction::Release:
                s_pipeline.onDataLost(now);
                s_pad.releaseAll();
                Serial.printf("[safe] no valid data for %lu ms - inputs released\n",
                              static_cast<unsigned long>(s_stats.msSinceLastValid(now)));
                break;
            case GateAction::Resume:
                Serial.println("[safe] valid data again - inputs live");
                break;
            case GateAction::None:
                break;
        }

        s_pad.update(now);
        const bool padConnected = s_pad.connected();
        if (padConnected != s_padWasConnected) {
            s_padWasConnected = padConnected;
            Serial.println(padConnected ? "[ble] host connected" : "[ble] host disconnected - advertising");
        }

        if (s_angles && now - lastAngles >= DEBUG_ANGLES_PERIOD_MS) {
            lastAngles = now;
            if (live) printAngles();
        }
        if (STATS_PERIOD_MS && (now - lastStats >= STATS_PERIOD_MS || s_statsRequest.exchange(false))) {
            lastStats = now;
            printStats(now);
        }
    }
}

}  // namespace

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(500);  // let the USB-serial monitor attach
    Serial.println("\n=== ESP32 phone motion controller - phases 2-4 ===");

    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);

    if (!netStartAccessPoint()) {
        Serial.println("[fatal] could not start AP");
        return;
    }
    s_rawQueue = netStartUdp();
    if (!s_rawQueue) {
        Serial.println("[fatal] could not start UDP task");
        return;
    }

    s_stats.reset(millis());
    s_pipeline.begin();
    s_pad.begin();
    Serial.printf("[ble] advertising as \"%s\" (HID gamepad)\n", BLE_DEVICE_NAME);

    xTaskCreatePinnedToCore(processTask, "process", TASK_STACK_PROCESS, nullptr, TASK_PRIO_PROCESS,
                            nullptr, TASK_CORE_APP);
    xTaskCreatePinnedToCore(serialTask, "serial", TASK_STACK_SERIAL, nullptr, TASK_PRIO_SERIAL,
                            nullptr, TASK_CORE_APP);

    Serial.printf("[cal] hold the phone flat and still for %lu ms\n",
                  static_cast<unsigned long>(CAL_DURATION_MS));
    Serial.println("[cmd] recal | raw | hex | angles | stats");
    digitalWrite(PIN_LED, HIGH);  // "AP up" indicator until Phase 6 LED states
}

void loop() {
    vTaskDelay(portMAX_DELAY);  // all work happens in tasks
}