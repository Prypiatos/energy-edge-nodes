#include "config.h"

#include "globals.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

#include <cstdio>
#include <cstring>

namespace {

constexpr std::size_t kConfigJsonCapacity = 1024;

void CopyString(char* destination, std::size_t destination_size, const char* source) {
    if (destination == nullptr || destination_size == 0) {
        return;
    }

    if (source == nullptr) {
        destination[0] = '\0';
        return;
    }

    std::snprintf(destination, destination_size, "%s", source);
}

bool IsSupportedNodeType(const char* node_type) {
    if (node_type == nullptr) {
        return false;
    }

    return std::strcmp(node_type, "plug") == 0 ||
           std::strcmp(node_type, "circuit") == 0 ||
           std::strcmp(node_type, "main") == 0;
}

RuntimeConfig BuildDefaultRuntimeConfig() {
    RuntimeConfig config = {};
    CopyString(config.node_id, sizeof(config.node_id), kDefaultNodeId);
    CopyString(config.node_type, sizeof(config.node_type), kDefaultNodeType);
    config.telemetry_interval_sec = kTelemetryPublishIntervalSec;
    config.health_interval_sec = kHealthPublishIntervalSec;
    config.current_warning_threshold = kDefaultCurrentWarningThreshold;
    config.current_critical_threshold = kDefaultCurrentCriticalThreshold;
    config.power_spike_delta = kDefaultPowerSpikeDelta;
    return config;
}

bool InitConfigStorage() {
    static bool initialized = false;

    if (initialized) {
        return true;
    }

    initialized = LittleFS.begin(true);
    return initialized;
}

bool ReadConfigDocument(const char* config_path, StaticJsonDocument<kConfigJsonCapacity>& document) {
    if (config_path == nullptr || config_path[0] == '\0') {
        return false;
    }

    if (!InitConfigStorage()) {
        return false;
    }

    File config_file = LittleFS.open(config_path, "r");
    if (!config_file) {
        return false;
    }

    const DeserializationError error = deserializeJson(document, config_file);
    config_file.close();
    return !error;
}

void ApplyConfigOverrides(JsonVariantConst root, RuntimeConfig* config) {
    if (config == nullptr || root.isNull()) {
        return;
    }

    const char* node_id = root["node_id"] | config->node_id;
    if (node_id[0] != '\0') {
        CopyString(config->node_id, sizeof(config->node_id), node_id);
    }

    const char* node_type = root["node_type"] | config->node_type;
    if (IsSupportedNodeType(node_type)) {
        CopyString(config->node_type, sizeof(config->node_type), node_type);
    }

    const char* wifi_ssid = root["wifi_ssid"] | config->wifi_ssid;
    const char* wifi_password = root["wifi_password"] | config->wifi_password;
    CopyString(config->wifi_ssid, sizeof(config->wifi_ssid), wifi_ssid);
    CopyString(config->wifi_password, sizeof(config->wifi_password), wifi_password);

    std::uint32_t telemetry_interval = config->telemetry_interval_sec;
    if (!root["telemetry_interval_sec"].isNull()) {
        telemetry_interval = root["telemetry_interval_sec"].as<std::uint32_t>();
    } else if (!root["publish_interval_sec"].isNull()) {
        telemetry_interval = root["publish_interval_sec"].as<std::uint32_t>();
    }
    const std::uint32_t health_interval = root["health_interval_sec"] | config->health_interval_sec;
    const float warning_threshold = root["current_warning_threshold"] | config->current_warning_threshold;
    const float critical_threshold = root["current_critical_threshold"] | config->current_critical_threshold;
    const float power_spike_delta = root["power_spike_delta"] | config->power_spike_delta;

    if (telemetry_interval > 0) {
        config->telemetry_interval_sec = telemetry_interval;
    }

    if (health_interval > 0) {
        config->health_interval_sec = health_interval;
    }

    if (warning_threshold > 0.0F) {
        config->current_warning_threshold = warning_threshold;
    }

    if (critical_threshold > 0.0F) {
        config->current_critical_threshold = critical_threshold;
    }

    if (power_spike_delta > 0.0F) {
        config->power_spike_delta = power_spike_delta;
    }
}

}  // namespace

RuntimeConfig GetDefaultRuntimeConfig() {
    return BuildDefaultRuntimeConfig();
}

bool InitRuntimeConfig(const char* config_path) {
    g_runtime_config = BuildDefaultRuntimeConfig();

    StaticJsonDocument<kConfigJsonCapacity> document;
    if (!ReadConfigDocument(config_path, document)) {
        return false;
    }

    ApplyConfigOverrides(document.as<JsonVariantConst>(), &g_runtime_config);
    return true;
}

bool SaveRuntimeConfig(const RuntimeConfig& config, const char* config_path) {
    if (config_path == nullptr || config_path[0] == '\0') {
        return false;
    }

    if (!InitConfigStorage()) {
        return false;
    }

    File config_file = LittleFS.open(config_path, "w");
    if (!config_file) {
        return false;
    }

    StaticJsonDocument<kConfigJsonCapacity> document;
    document["node_id"] = config.node_id;
    document["node_type"] = config.node_type;
    document["wifi_ssid"] = config.wifi_ssid;
    document["wifi_password"] = config.wifi_password;
    document["telemetry_interval_sec"] = config.telemetry_interval_sec;
    document["health_interval_sec"] = config.health_interval_sec;
    document["current_warning_threshold"] = config.current_warning_threshold;
    document["current_critical_threshold"] = config.current_critical_threshold;
    document["power_spike_delta"] = config.power_spike_delta;

    const std::size_t bytes_written = serializeJsonPretty(document, config_file);
    config_file.close();
    return bytes_written > 0;
}

void BuildRuntimeConfigJson(const RuntimeConfig& config, char* buffer, std::size_t buffer_size) {
    if (buffer == nullptr || buffer_size == 0) {
        return;
    }

    StaticJsonDocument<kConfigJsonCapacity> document;
    document["node_id"] = config.node_id;
    document["node_type"] = config.node_type;
    document["wifi_ssid"] = config.wifi_ssid;
    document["wifi_password"] = config.wifi_password;
    document["telemetry_interval_sec"] = config.telemetry_interval_sec;
    document["health_interval_sec"] = config.health_interval_sec;
    document["current_warning_threshold"] = config.current_warning_threshold;
    document["current_critical_threshold"] = config.current_critical_threshold;
    document["power_spike_delta"] = config.power_spike_delta;

    serializeJson(document, buffer, buffer_size);
}

const char* GetNodeId() {
    return g_runtime_config.node_id[0] == '\0' ? kDefaultNodeId : g_runtime_config.node_id;
}

const char* GetNodeType() {
    return IsSupportedNodeType(g_runtime_config.node_type) ? g_runtime_config.node_type : kDefaultNodeType;
}

bool HasNodeIdentity() {
    return g_runtime_config.node_id[0] != '\0' && std::strcmp(GetNodeId(), kDefaultNodeId) != 0;
}

bool HasWifiCredentials() {
    return g_runtime_config.wifi_ssid[0] != '\0' && g_runtime_config.wifi_password[0] != '\0';
}
