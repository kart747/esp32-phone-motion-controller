// packet_stats - receive rate, malformed/dropped counters and the
// "no valid sample" watchdog. Pure logic: the caller passes in the arrival
// millisecond timestamp, which makes it testable off-target.
#pragma once

#include <stddef.h>
#include <stdint.h>

class PacketStats {
public:
    PacketStats() { reset(0); }
    void reset(uint32_t nowMs);

    // One UDP datagram arrived. Does NOT refresh the watchdog: traffic that
    // never parses must not keep the stream looking alive.
    void onDatagram(uint32_t nowMs);
    // Result of parsing that datagram.
    void onParsed(size_t good, size_t malformed, size_t overflow, bool truncated);
    // One sample ready for the filter (refreshes the watchdog).
    void onValidSample(uint32_t rxMs);
    // Running total from the network layer (rx queue full).
    void setQueueDrops(uint32_t total) { queueDrops_ = total; }

    // Roll the rate window; call regularly (e.g. every process-task wake).
    void tick(uint32_t nowMs);

    float    packetsPerSec() const { return pps_; }
    float    samplesPerSec() const { return sps_; }
    uint32_t datagrams() const { return datagrams_; }
    uint32_t samples() const { return samples_; }
    uint32_t malformed() const { return malformed_; }
    uint32_t overflow() const { return overflow_; }
    uint32_t truncated() const { return truncated_; }
    uint32_t queueDrops() const { return queueDrops_; }

    bool     everValid() const { return everValid_; }
    // UINT32_MAX if no valid sample has arrived yet.
    uint32_t msSinceLastValid(uint32_t nowMs) const;
    // True if no valid sample has arrived for more than timeoutMs (or ever).
    bool     isStale(uint32_t nowMs, uint32_t timeoutMs) const;

private:
    uint32_t datagrams_, samples_, malformed_, overflow_, truncated_, queueDrops_;
    uint32_t lastValidMs_;
    bool     everValid_;
    uint32_t windowStartMs_, windowDatagrams_, windowSamples_;
    float    pps_, sps_;
};