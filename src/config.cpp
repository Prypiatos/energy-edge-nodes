#include "config.h"

#include "globals.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr std::size_t kConfigBufferSize = 1024;

RuntimeConfig BuildDefaultRuntimeConfig() {
    return RuntimeConfig{
        kTelemetryPublishIntervalSec,
        kHealthPublishIntervalSec,
        kDefaultCurrentWarningThreshold,
        kDefaultCurrentCriticalThreshold,
        kDefaultPowerSpikeDelta,
    };
}

bool ReadConfigFile(const char* config_path, char* buffer, std::size_t buffer_size) {
    if (config_path == nullptr || buffer == nullptr || buffer_size < 2) {
        return false;
    }

    std::FILE* config_file = std::fopen(config_path, "rb");
    if (config_file == nullptr) {
        return false;
    }

    const std::size_t bytes_read = std::fread(buffer, 1, buffer_size - 1, config_file);
    buffer[bytes_read] = '\0';
    std::fclose(config_file);
    return bytes_read > 0;
}

const char* FindJsonValue(const char* json, const char* key) {
    const char* key_position = std::strstr(json, key);
    if (key_position == nullptr) {
        return nullptr;
    }

    const char* colon = std::strchr(key_position, ':');
    if (colon == nullptr) {
        return nullptr;
    }

    ++colon;
    while (*colon == ' ' || *colon == '\t' || *colon == '\n' || *colon == '\r') {
        ++colon;
    }

    return colon;
}

bool TryParseUintField(const char* json, const char* key, std::uint32_t* value) {
    const char* field = FindJsonValue(json, key);
    if (field == nullptr) {
        return false;
    }

    char* end = nullptr;
    const unsigned long parsed = std::strtoul(field, &end, 10);
    if (end == field) {
        return false;
    }

    *value = static_cast<std::uint32_t>(parsed);
    return true;
}

bool TryParseFloatField(const char* json, const char* key, float* value) {
    const char* field = FindJsonValue(json, key);
    if (field == nullptr) {
        return false;
    }

    char* end = nullptr;
    const float parsed = std::strtof(field, &end);
    if (end == field) {
        return false;
    }

    *value = parsed;
    return true;
}

void ApplyConfigOverrides(const char* json, RuntimeConfig* config) {
    if (json == nullptr || config == nullptr) {
        return;
    }

    if (!TryParseUintField(json, "\"telemetry_interval_sec\"", &config->telemetry_interval_sec)) {
        TryParseUintField(json, "\"publish_interval_sec\"", &config->telemetry_interval_sec);
    }

    TryParseUintField(json, "\"health_interval_sec\"", &config->health_interval_sec);
    TryParseFloatField(json, "\"current_warning_threshold\"", &config->current_warning_threshold);
    TryParseFloatField(json, "\"current_critical_threshold\"", &config->current_critical_threshold);
    TryParseFloatField(json, "\"power_spike_delta\"", &config->power_spike_delta);
}

}  // namespace

RuntimeConfig GetDefaultRuntimeConfig() {
    return BuildDefaultRuntimeConfig();
}

bool InitRuntimeConfig(const char* config_path) {
    g_runtime_config = BuildDefaultRuntimeConfig();

    char config_buffer[kConfigBufferSize];
    if (!ReadConfigFile(config_path, config_buffer, sizeof(config_buffer))) {
        return false;
    }

    ApplyConfigOverrides(config_buffer, &g_runtime_config);
    return true;
}
