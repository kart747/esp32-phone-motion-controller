// net_udp - Wi-Fi SoftAP bring-up and the UDP receive task.
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "config.h"

// One received UDP datagram, copied out of the Wi-Fi stack so the rx task
// never blocks on a slow consumer. Plain-old-data: safe for FreeRTOS queues.
struct RawPacket {
    uint32_t rxMicros;           // micros() when the datagram was read
    uint32_t srcIp;              // IPv4, network byte order as IPAddress uint32
    uint16_t srcPort;
    uint16_t len;                // bytes stored in data (<= UDP_MAX_PAYLOAD)
    bool     truncated;          // datagram was larger than UDP_MAX_PAYLOAD
    uint8_t  data[UDP_MAX_PAYLOAD];
};

// Start the SoftAP with the settings in config.h. Returns false on failure.
bool netStartAccessPoint();

// Create the packet queue and start the UDP receive task. Returns the queue
// that RawPackets are pushed to, or nullptr on failure.
QueueHandle_t netStartUdp();

// Counters maintained by the rx task (read-only for others).
uint32_t netPacketsReceived();
uint32_t netPacketsDroppedQueueFull();
