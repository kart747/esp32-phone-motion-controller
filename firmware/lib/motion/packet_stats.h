// packet_stats - receive-rate, malformed/dropped counters and the
// "no data" watchdog. Pure logic: the caller passes in millis().
#pragma once

#include <stdint.h>

class PacketStats {
public:
    PacketStats() { reset(0); }
    void reset(uint32_t nowMs);

    // One UDP datagram arrived (call once per datagram).
    void onDatagram(uint32_t nowMs);
    // Result of parsing that datagram.
    void onParsed(uint32_t goodSamples, uint32_t malformedLines);
    // Datagrams lost before parsing (e.g. rx queue full); the caller reports
    // the running total from the network layer.
    void setQueueDrops(uint32_t total) { queueDrops_ = total; }

    // Call regularly (e.g. every process-task wake) to roll the rate window.
    void tick(uint32_t nowMs);

    float    packetsPerSec() const { return pps_; }
    float    samplesPerSec() const { return sps_; }
    uint32_t datagrams() const { return datagrams_; }
    uint32_t samples() const { return samples_; }
    uint32_t malformed() const { return malformed_; }
    uint32_t queueDrops() const { return queueDrops_; }

    bool     everReceived() const { return everReceived_; }
    // UINT32_MAX if nothing has been received yet.
    uint32_t msSinceLast(uint32_t nowMs) const;
    // True if nothing has arrived for more than timeoutMs (or ever).
    bool     isStale(uint32_t nowMs, uint32_t timeoutMs) const;

private:
    uint32_t datagrams_, samples_, malformed_, queueDrops_;
    uint32_t lastRxMs_;
    bool     everReceived_;
    uint32_t windowStartMs_, windowDatagrams_, windowSamples_;
    float    pps_, sps_;
};
