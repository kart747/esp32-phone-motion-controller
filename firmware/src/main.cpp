// Phase 1: SoftAP + raw UDP dump to serial.
// Later phases replace the dump in processTask with parser -> filter ->
// gesture pipeline -> gamepad output.
#include <Arduino.h>
#include <IPAddress.h>

#include "config.h"
#include "net_udp.h"

static QueueHandle_t s_rawQueue = nullptr;

// Print a payload as text, escaping non-printable bytes so binary formats are
// still readable and a stray control char can't mess up the terminal.
static void printEscaped(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        const uint8_t c = data[i];
        if (c == '\n')       Serial.print("\\n");
        else if (c == '\r')  Serial.print("\\r");
        else if (c >= 0x20 && c < 0x7F) Serial.write(c);
        else                 Serial.printf("\\x%02X", c);
    }
}

static void printHex(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) Serial.printf("%02X ", data[i]);
}

// Consumer task. In Phase 1 it only dumps what arrived.
static void processTask(void *) {
    static RawPacket pkt;
    uint32_t lastStats = millis();
    for (;;) {
        if (xQueueReceive(s_rawQueue, &pkt, pdMS_TO_TICKS(STATS_PERIOD_MS)) == pdTRUE) {
            Serial.printf("[rx %lu us] %s:%u len=%u%s | ", static_cast<unsigned long>(pkt.rxMicros),
                          IPAddress(pkt.srcIp).toString().c_str(), pkt.srcPort, pkt.len,
                          pkt.truncated ? " TRUNCATED" : "");
            printEscaped(pkt.data, pkt.len);
            Serial.println();
            if (DUMP_HEX) {
                Serial.print("    hex: ");
                printHex(pkt.data, pkt.len);
                Serial.println();
            }
        }
        if (millis() - lastStats >= STATS_PERIOD_MS) {
            lastStats = millis();
            Serial.printf("[stats] received=%lu dropped(queue full)=%lu\n",
                          static_cast<unsigned long>(netPacketsReceived()),
                          static_cast<unsigned long>(netPacketsDroppedQueueFull()));
        }
    }
}

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(500);  // let the USB-serial monitor attach
    Serial.println("\n=== ESP32 phone motion controller - Phase 1 ===");

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
    xTaskCreatePinnedToCore(processTask, "process", TASK_STACK_PROCESS, nullptr,
                            TASK_PRIO_PROCESS, nullptr, TASK_CORE_APP);
    digitalWrite(PIN_LED, HIGH);  // simple "AP up" indicator until Phase 6 LED states
}

void loop() {
    vTaskDelay(portMAX_DELAY);  // all work happens in tasks
}
