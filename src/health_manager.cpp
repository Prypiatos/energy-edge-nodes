#include "health_manager.h"

#include <Arduino.h>

#include "buffer_manager.h"
#include "config.h"
#include "globals.h"
#include "mqtt_manager.h"
#include "time_manager.h"

#include <cstdio>

namespace {

constexpr char kHealthTopicTemplate[] = "energy/nodes/%s/health";
constexpr std::size_t kIsoTimestampSize = 32;

unsigned long g_last_health_publish_ms = 0;

void BuildHealthTopic(char* topic, std::size_t topic_size) {
    if (topic == nullptr || topic_size == 0) {
        return;
    }

    std::snprintf(topic, topic_size, kHealthTopicTemplate, GetNodeId());
}

void BuildHealthPayload(std::uint32_t sequence_no,
                        bool buffered,
                        char* payload,
                        std::size_t payload_size) {
    if (payload == nullptr || payload_size == 0) {
        return;
    }

    char timestamp[kIsoTimestampSize] = {};
    FormatTimestampISO8601(GetCurrentTimestampSec(), timestamp, sizeof(timestamp));

    std::snprintf(payload,
                  payload_size,
                  "{\"node_id\":\"%s\",\"node_type\":\"%s\",\"timestamp\":\"%s\","
                  "\"sequence_no\":%lu,\"status\":\"%s\",\"uptime_sec\":%lu,"
                  "\"mqtt_connected\":%s,\"wifi_connected\":%s,\"sensor_ok\":%s,"
                  "\"buffered_count\":%lu,\"buffered\":%s}",
                  GetNodeId(),
                  GetNodeType(),
                  timestamp,
                  static_cast<unsigned long>(sequence_no),
                  g_system_state.status,
                  static_cast<unsigned long>(g_system_state.uptime_sec),
                  g_system_state.mqtt_connected ? "true" : "false",
                  g_system_state.wifi_connected ? "true" : "false",
                  g_system_state.sensor_ok ? "true" : "false",
                  static_cast<unsigned long>(g_system_state.buffered_count),
                  buffered ? "true" : "false");
}

}  // namespace

void InitHealthManager() {
    g_last_health_publish_ms = 0;
}

void RunHealthTask() {
    const unsigned long interval_ms = g_runtime_config.health_interval_sec * 1000UL;
    const unsigned long now = millis();
    if (g_last_health_publish_ms != 0 && (now - g_last_health_publish_ms) < interval_ms) {
        return;
    }

    g_last_health_publish_ms = now;

    const std::uint32_t sequence_no = ++g_system_state.health_sequence_no;

    char topic[kOutgoingTopicMaxLength] = {};
    BuildHealthTopic(topic, sizeof(topic));

    char payload[kOutgoingPayloadMaxLength] = {};
    BuildHealthPayload(sequence_no, false, payload, sizeof(payload));
    if (MqttPublish(topic, payload)) {
        return;
    }

    OutgoingMessage message = {};
    std::snprintf(message.topic, sizeof(message.topic), "%s", topic);
    BuildHealthPayload(sequence_no, true, message.payload, sizeof(message.payload));
    message.buffered = true;
    EnqueueOutgoingMessage(message);
}
