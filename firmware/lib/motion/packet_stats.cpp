#include "packet_stats.h"

#include "config.h"

void PacketStats::reset(uint32_t nowMs) {
    datagrams_ = samples_ = malformed_ = queueDrops_ = 0;
    lastRxMs_ = nowMs;
    everReceived_ = false;
    windowStartMs_ = nowMs;
    windowDatagrams_ = windowSamples_ = 0;
    pps_ = sps_ = 0.0f;
}

void PacketStats::onDatagram(uint32_t nowMs) {
    datagrams_++;
    windowDatagrams_++;
    lastRxMs_ = nowMs;
    everReceived_ = true;
}

void PacketStats::onParsed(uint32_t goodSamples, uint32_t malformedLines) {
    samples_ += goodSamples;
    windowSamples_ += goodSamples;
    malformed_ += malformedLines;
}

void PacketStats::tick(uint32_t nowMs) {
    const uint32_t elapsed = nowMs - windowStartMs_;  // unsigned: safe across millis() wrap
    if (elapsed >= RATE_WINDOW_MS) {
        const float sec = elapsed / 1000.0f;
        pps_ = windowDatagrams_ / sec;
        sps_ = windowSamples_ / sec;
        windowStartMs_ = nowMs;
        windowDatagrams_ = windowSamples_ = 0;
    }
}

uint32_t PacketStats::msSinceLast(uint32_t nowMs) const {
    return everReceived_ ? nowMs - lastRxMs_ : UINT32_MAX;
}

bool PacketStats::isStale(uint32_t nowMs, uint32_t timeoutMs) const {
    return !everReceived_ || (nowMs - lastRxMs_) > timeoutMs;
}
