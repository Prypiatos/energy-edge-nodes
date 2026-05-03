#pragma once

#include <cstddef>
#include <cstdint>

void InitTimeManager();
void SyncTimeIfNeeded();
std::uint32_t GetCurrentTimestampSec();
std::uint64_t GetCurrentTimestampMs();
void FormatTimestampISO8601(std::uint32_t timestamp_sec, char* buffer, std::size_t buffer_size);
