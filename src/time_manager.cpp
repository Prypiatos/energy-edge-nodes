#include "time_manager.h"

#include <Arduino.h>
#include <WiFi.h>

#include <ctime>

namespace {

constexpr unsigned long kTimeSyncRetryIntervalMs = 300000;

bool g_time_sync_initialized = false;
unsigned long g_last_time_sync_attempt_ms = 0;

bool HasNetworkTime() {
    return time(nullptr) > 946684800;
}

}  // namespace

void InitTimeManager() {
    g_time_sync_initialized = false;
    g_last_time_sync_attempt_ms = 0;
}

void SyncTimeIfNeeded() {
    if (WiFi.status() != WL_CONNECTED || HasNetworkTime()) {
        return;
    }

    const unsigned long now = millis();
    if (g_last_time_sync_attempt_ms != 0 && (now - g_last_time_sync_attempt_ms) < kTimeSyncRetryIntervalMs) {
        return;
    }

    g_last_time_sync_attempt_ms = now;
    if (!g_time_sync_initialized) {
        configTzTime("UTC0", "pool.ntp.org", "time.nist.gov");
        g_time_sync_initialized = true;
    }
}

std::uint32_t GetCurrentTimestampSec() {
    const time_t now = time(nullptr);
    if (now > 0) {
        return static_cast<std::uint32_t>(now);
    }

    return millis() / 1000U;
}

std::uint64_t GetCurrentTimestampMs() {
    const time_t now = time(nullptr);
    if (now > 0) {
        return static_cast<std::uint64_t>(now) * 1000ULL;
    }

    return static_cast<std::uint64_t>(millis());
}

void FormatTimestampISO8601(std::uint32_t timestamp_sec, char* buffer, std::size_t buffer_size) {
    if (buffer == nullptr || buffer_size == 0) {
        return;
    }

    const time_t timestamp = static_cast<time_t>(timestamp_sec);
    struct tm tm_info = {};
    gmtime_r(&timestamp, &tm_info);
    strftime(buffer, buffer_size, "%Y-%m-%dT%H:%M:%SZ", &tm_info);
}
