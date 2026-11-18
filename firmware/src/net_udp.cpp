#include "net_udp.h"

#include <WiFi.h>
#include <WiFiUdp.h>

static QueueHandle_t s_queue = nullptr;
static volatile uint32_t s_received = 0;
static volatile uint32_t s_droppedQueueFull = 0;

// Log who joins/leaves the AP; very useful when debugging phone connection.
static void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
            Serial.printf("[wifi] station connected: " MACSTR "\n",
                          MAC2STR(info.wifi_ap_staconnected.mac));
            break;
        case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
            Serial.printf("[wifi] station disconnected: " MACSTR "\n",
                          MAC2STR(info.wifi_ap_stadisconnected.mac));
            break;
        case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
            Serial.printf("[wifi] station got IP: %s\n",
                          IPAddress(info.wifi_ap_staipassigned.ip.addr).toString().c_str());
            break;
        default:
            break;
    }
}

bool netStartAccessPoint() {
    WiFi.onEvent(onWifiEvent);
    WiFi.mode(WIFI_AP);

    const IPAddress ip(AP_IP[0], AP_IP[1], AP_IP[2], AP_IP[3]);
    const IPAddress gw(AP_GATEWAY[0], AP_GATEWAY[1], AP_GATEWAY[2], AP_GATEWAY[3]);
    const IPAddress mask(AP_SUBNET[0], AP_SUBNET[1], AP_SUBNET[2], AP_SUBNET[3]);
    if (!WiFi.softAPConfig(ip, gw, mask)) {
        Serial.println("[wifi] softAPConfig failed");
        return false;
    }
    if (!WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, /*hidden=*/false, AP_MAX_CLIENTS)) {
        Serial.println("[wifi] softAP failed");
        return false;
    }
    Serial.printf("[wifi] AP up: SSID=\"%s\" channel=%u IP=%s\n", AP_SSID, AP_CHANNEL,
                  WiFi.softAPIP().toString().c_str());
    return true;
}

// Receive task: poll the socket, copy each datagram into a RawPacket and
// push it to the queue WITHOUT blocking. If the consumer is behind, drop the
// newest packet and count it (stale motion data is worse than missing data).
static void udpTask(void *) {
    WiFiUDP udp;
    if (!udp.begin(UDP_PORT)) {
        Serial.printf("[udp] failed to bind port %u\n", UDP_PORT);
        vTaskDelete(nullptr);
        return;
    }
    Serial.printf("[udp] listening on port %u\n", UDP_PORT);

    static RawPacket pkt;  // static: keeps ~0.5 KB off the task stack
    for (;;) {
        const int avail = udp.parsePacket();
        if (avail <= 0) {
            vTaskDelay(pdMS_TO_TICKS(UDP_POLL_INTERVAL_MS));
            continue;
        }
        pkt.rxMicros = micros();
        pkt.srcIp = static_cast<uint32_t>(udp.remoteIP());
        pkt.srcPort = udp.remotePort();
        const int n = udp.read(pkt.data, UDP_MAX_PAYLOAD);
        pkt.len = n > 0 ? static_cast<uint16_t>(n) : 0;
        pkt.truncated = static_cast<size_t>(avail) > UDP_MAX_PAYLOAD;
        s_received++;

        if (xQueueSend(s_queue, &pkt, 0) != pdTRUE) {
            s_droppedQueueFull++;
        }
    }
}

QueueHandle_t netStartUdp() {
    s_queue = xQueueCreate(UDP_QUEUE_LEN, sizeof(RawPacket));
    if (!s_queue) return nullptr;
    if (xTaskCreatePinnedToCore(udpTask, "udp_rx", TASK_STACK_UDP, nullptr, TASK_PRIO_UDP,
                                nullptr, TASK_CORE_APP) != pdPASS) {
        return nullptr;
    }
    return s_queue;
}

uint32_t netPacketsReceived() { return s_received; }
uint32_t netPacketsDroppedQueueFull() { return s_droppedQueueFull; }
