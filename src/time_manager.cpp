#include "time_manager.h"

#include <Arduino.h>
#include <WiFi.h>

#include <cstdlib>
#include <cstring>
#include <ctime>

namespace {

constexpr unsigned long kTimeSyncRetryIntervalMs = 300000;
constexpr time_t kMinimumValidEpochSec = 946684800;

bool g_time_sync_initialized = false;
unsigned long g_last_time_sync_attempt_ms = 0;
time_t g_fallback_epoch_sec = kMinimumValidEpochSec;
unsigned long g_fallback_epoch_anchor_ms = 0;

int ParseMonth(const char* month) {
    static constexpr const char* kMonths[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    if (month == nullptr) {
        return 0;
    }

    for (int index = 0; index < 12; ++index) {
        if (std::strncmp(month, kMonths[index], 3) == 0) {
            return index;
        }
    }

    return 0;
}

time_t BuildFallbackEpochSec() {
    struct tm build_tm = {};
    build_tm.tm_year = std::atoi(__DATE__ + 7) - 1900;
    build_tm.tm_mon = ParseMonth(__DATE__);
    build_tm.tm_mday = (__DATE__[4] == ' ')
        ? (__DATE__[5] - '0')
        : ((__DATE__[4] - '0') * 10) + (__DATE__[5] - '0');
    build_tm.tm_hour = ((__TIME__[0] - '0') * 10) + (__TIME__[1] - '0');
    build_tm.tm_min = ((__TIME__[3] - '0') * 10) + (__TIME__[4] - '0');
    build_tm.tm_sec = ((__TIME__[6] - '0') * 10) + (__TIME__[7] - '0');

    const time_t build_time = mktime(&build_tm);
    if (build_time > kMinimumValidEpochSec) {
        return build_time;
    }

    return kMinimumValidEpochSec;
}

std::uint64_t GetFallbackTimestampMs() {
    return (static_cast<std::uint64_t>(g_fallback_epoch_sec) * 1000ULL) +
           static_cast<std::uint64_t>(millis() - g_fallback_epoch_anchor_ms);
}

bool HasNetworkTime() {
    return time(nullptr) > kMinimumValidEpochSec;
}

}  // namespace

void InitTimeManager() {
    g_time_sync_initialized = false;
    g_last_time_sync_attempt_ms = 0;
    g_fallback_epoch_sec = BuildFallbackEpochSec();
    g_fallback_epoch_anchor_ms = millis();
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
    if (now > kMinimumValidEpochSec) {
        return static_cast<std::uint32_t>(now);
    }

    return static_cast<std::uint32_t>(GetFallbackTimestampMs() / 1000ULL);
}

std::uint64_t GetCurrentTimestampMs() {
    const time_t now = time(nullptr);
    if (now > kMinimumValidEpochSec) {
        return static_cast<std::uint64_t>(now) * 1000ULL;
    }

    return GetFallbackTimestampMs();
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
