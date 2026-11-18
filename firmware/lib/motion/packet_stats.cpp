#include "packet_stats.h"

#include "config.h"

void PacketStats::reset(uint32_t nowMs) {
    datagrams_ = samples_ = malformed_ = overflow_ = truncated_ = queueDrops_ = 0;
    lastValidMs_ = nowMs;
    everValid_ = false;
    windowStartMs_ = nowMs;
    windowDatagrams_ = windowSamples_ = 0;
    pps_ = sps_ = 0.0f;
}

void PacketStats::onDatagram(uint32_t nowMs) {
    (void)nowMs;  // arrival time alone does not prove the stream is healthy
    datagrams_++;
    windowDatagrams_++;
}

void PacketStats::onParsed(size_t good, size_t malformed, size_t overflow, bool truncated) {
    samples_ += static_cast<uint32_t>(good);
    windowSamples_ += static_cast<uint32_t>(good);
    malformed_ += static_cast<uint32_t>(malformed);
    overflow_ += static_cast<uint32_t>(overflow);
    if (truncated) truncated_++;
}

void PacketStats::onValidSample(uint32_t rxMs) {
    lastValidMs_ = rxMs;
    everValid_ = true;
}

void PacketStats::tick(uint32_t nowMs) {
    const uint32_t elapsed = nowMs - windowStartMs_;  // unsigned: safe across wrap
    if (elapsed >= RATE_WINDOW_MS) {
        const float sec = elapsed / 1000.0f;
        pps_ = windowDatagrams_ / sec;
        sps_ = windowSamples_ / sec;
        windowStartMs_ = nowMs;
        windowDatagrams_ = windowSamples_ = 0;
    }
}

uint32_t PacketStats::msSinceLastValid(uint32_t nowMs) const {
    return everValid_ ? nowMs - lastValidMs_ : UINT32_MAX;
}

bool PacketStats::isStale(uint32_t nowMs, uint32_t timeoutMs) const {
    return !everValid_ || (nowMs - lastValidMs_) > timeoutMs;
}