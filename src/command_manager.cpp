#include "command_manager.h"

#include "buffer_manager.h"
#include "config.h"
#include "globals.h"
#include "mqtt_manager.h"
#include "time_manager.h"

#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>

namespace {

constexpr std::size_t kCommandTopicSize = 128;
constexpr std::size_t kCommandPayloadSize = 512;
constexpr std::size_t kStatusTimestampSize = 32;
constexpr std::size_t kCommandJsonCapacity = 768;

constexpr char kGetStatusTopicTemplate[] = "energy/nodes/%s/cmd/get_status";
constexpr char kConfigTopicTemplate[] = "energy/nodes/%s/cmd/config";
constexpr char kStatusTopicTemplate[] = "energy/nodes/%s/status";

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

void BuildTopic(char* topic, std::size_t topic_size, const char* topic_template) {
    if (topic == nullptr || topic_size == 0 || topic_template == nullptr) {
        return;
    }

    std::snprintf(topic, topic_size, topic_template, GetNodeId());
}

bool PublishOrBuffer(const char* topic, const char* payload) {
    if (topic == nullptr || payload == nullptr) {
        return false;
    }

    if (MqttPublish(topic, payload)) {
        return true;
    }

    OutgoingMessage message = {};
    CopyString(message.topic, sizeof(message.topic), topic);
    CopyString(message.payload, sizeof(message.payload), payload);
    message.buffered = true;
    return EnqueueOutgoingMessage(message);
}

bool HandleGetStatusCommand(JsonVariantConst root) {
    char request_id[64] = {};
    CopyString(request_id, sizeof(request_id), root["request_id"] | "");

    char topic[kOutgoingTopicMaxLength] = {};
    BuildTopic(topic, sizeof(topic), kStatusTopicTemplate);

    char timestamp[kStatusTimestampSize] = {};
    FormatTimestampISO8601(GetCurrentTimestampSec(), timestamp, sizeof(timestamp));

    const SensorSample sample = g_latest_sample;

    char payload[kOutgoingPayloadMaxLength] = {};
    std::snprintf(payload,
                  sizeof(payload),
                  "{\"request_id\":\"%s\",\"node_id\":\"%s\",\"node_type\":\"%s\","
                  "\"timestamp\":\"%s\",\"status\":\"%s\",\"latest_voltage\":%.2f,"
                  "\"latest_current\":%.3f,\"latest_power\":%.2f,\"latest_energy_wh\":%.3f,"
                  "\"latest_frequency\":%.2f,\"latest_power_factor\":%.3f,\"sensor_ok\":%s}",
                  request_id,
                  GetNodeId(),
                  GetNodeType(),
                  timestamp,
                  g_system_state.status,
                  static_cast<double>(sample.voltage),
                  static_cast<double>(sample.current),
                  static_cast<double>(sample.power),
                  static_cast<double>(sample.energy_wh),
                  static_cast<double>(sample.frequency_hz),
                  static_cast<double>(sample.power_factor),
                  g_system_state.sensor_ok ? "true" : "false");

    return PublishOrBuffer(topic, payload);
}

bool ApplySupportedConfigFields(JsonVariantConst root, RuntimeConfig* updated_config) {
    if (updated_config == nullptr) {
        return false;
    }

    bool changed = false;

    const bool has_wifi_ssid = !root["wifi_ssid"].isNull();
    const bool has_wifi_password = !root["wifi_password"].isNull();
    if (has_wifi_ssid != has_wifi_password) {
        return false;
    }

    if (has_wifi_ssid && has_wifi_password) {
        const char* wifi_ssid = root["wifi_ssid"].as<const char*>();
        const char* wifi_password = root["wifi_password"].as<const char*>();
        if (wifi_ssid == nullptr || wifi_password == nullptr || wifi_ssid[0] == '\0' || wifi_password[0] == '\0') {
            return false;
        }

        CopyString(updated_config->wifi_ssid, sizeof(updated_config->wifi_ssid), wifi_ssid);
        CopyString(updated_config->wifi_password, sizeof(updated_config->wifi_password), wifi_password);
        changed = true;
    }

    if (!root["telemetry_interval_sec"].isNull()) {
        const std::uint32_t value = root["telemetry_interval_sec"].as<std::uint32_t>();
        if (value == 0) {
            return false;
        }
        updated_config->telemetry_interval_sec = value;
        changed = true;
    } else if (!root["publish_interval_sec"].isNull()) {
        const std::uint32_t value = root["publish_interval_sec"].as<std::uint32_t>();
        if (value == 0) {
            return false;
        }
        updated_config->telemetry_interval_sec = value;
        changed = true;
    }

    if (!root["health_interval_sec"].isNull()) {
        const std::uint32_t value = root["health_interval_sec"].as<std::uint32_t>();
        if (value == 0) {
            return false;
        }
        updated_config->health_interval_sec = value;
        changed = true;
    }

    if (!root["current_warning_threshold"].isNull()) {
        const float value = root["current_warning_threshold"].as<float>();
        if (value <= 0.0F) {
            return false;
        }
        updated_config->current_warning_threshold = value;
        changed = true;
    }

    if (!root["current_critical_threshold"].isNull()) {
        const float value = root["current_critical_threshold"].as<float>();
        if (value <= 0.0F) {
            return false;
        }
        updated_config->current_critical_threshold = value;
        changed = true;
    }

    if (!root["power_spike_delta"].isNull()) {
        const float value = root["power_spike_delta"].as<float>();
        if (value <= 0.0F) {
            return false;
        }
        updated_config->power_spike_delta = value;
        changed = true;
    }

    if (updated_config->current_warning_threshold > updated_config->current_critical_threshold) {
        return false;
    }

    return changed;
}

bool HandleConfigCommand(JsonVariantConst root) {
    RuntimeConfig updated_config = g_runtime_config;
    if (!ApplySupportedConfigFields(root, &updated_config)) {
        return false;
    }

    if (!SaveRuntimeConfig(updated_config)) {
        return false;
    }

    g_runtime_config = updated_config;
    UpdateSystemStatus();
    return true;
}

void HandleCommand(const char* topic, const char* payload) {
    if (topic == nullptr || payload == nullptr) {
        return;
    }

    StaticJsonDocument<kCommandJsonCapacity> document;
    const DeserializationError error = deserializeJson(document, payload);
    if (error) {
        return;
    }

    char get_status_topic[kCommandTopicSize] = {};
    char config_topic[kCommandTopicSize] = {};
    BuildTopic(get_status_topic, sizeof(get_status_topic), kGetStatusTopicTemplate);
    BuildTopic(config_topic, sizeof(config_topic), kConfigTopicTemplate);

    if (std::strcmp(topic, get_status_topic) == 0) {
        HandleGetStatusCommand(document.as<JsonVariantConst>());
        return;
    }

    if (std::strcmp(topic, config_topic) == 0) {
        HandleConfigCommand(document.as<JsonVariantConst>());
    }
}

}  // namespace

void InitCommandManager() {}

void RunCommandTask() {
    char topic[kCommandTopicSize] = {};
    char payload[kCommandPayloadSize] = {};
    while (ConsumePendingMqttCommand(topic, sizeof(topic), payload, sizeof(payload))) {
        HandleCommand(topic, payload);
    }
}
