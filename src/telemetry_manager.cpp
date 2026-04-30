#include "telemetry_manager.h"

#include <Arduino.h>

#include "buffer_manager.h"
#include "config.h"
#include "globals.h"
#include "mqtt_manager.h"
#include "time_manager.h"

#include <cstdio>

namespace {

constexpr char kTelemetryTopicTemplate[] = "energy/nodes/%s/telemetry";

unsigned long g_last_telemetry_publish_ms = 0;
std::uint32_t g_last_telemetry_sample_timestamp = 0;

void BuildTelemetryTopic(char* topic, std::size_t topic_size) {
    if (topic == nullptr || topic_size == 0) {
        return;
    }

    std::snprintf(topic, topic_size, kTelemetryTopicTemplate, GetNodeId());
}

void BuildTelemetryPayload(const SensorSample& sample,
                           std::uint32_t sequence_no,
                           bool buffered,
                           char* payload,
                           std::size_t payload_size) {
    if (payload == nullptr || payload_size == 0) {
        return;
    }

    const unsigned long long timestamp_ms = static_cast<unsigned long long>(GetCurrentTimestampMs());
    std::snprintf(payload,
                  payload_size,
                  "{\"node_id\":\"%s\",\"timestamp\":%llu,\"sequence_no\":%lu,"
                  "\"voltage\":%.2f,\"current\":%.3f,\"power\":%.2f,\"energy_wh\":%.3f,"
                  "\"buffered\":%s}",
                  GetNodeId(),
                  timestamp_ms,
                  static_cast<unsigned long>(sequence_no),
                  static_cast<double>(sample.voltage),
                  static_cast<double>(sample.current),
                  static_cast<double>(sample.power),
                  static_cast<double>(sample.energy_wh),
                  buffered ? "true" : "false");
}

}  // namespace

void InitTelemetryManager() {
    g_last_telemetry_publish_ms = 0;
    g_last_telemetry_sample_timestamp = 0;
}

void RunTelemetryTask() {
    const SensorSample sample = g_latest_sample;
    if (!sample.valid) {
        return;
    }

    const unsigned long interval_ms = g_runtime_config.telemetry_interval_sec * 1000UL;
    const unsigned long now = millis();
    if (g_last_telemetry_publish_ms != 0 && (now - g_last_telemetry_publish_ms) < interval_ms) {
        return;
    }

    if (sample.timestamp == g_last_telemetry_sample_timestamp) {
        return;
    }

    g_last_telemetry_publish_ms = now;
    g_last_telemetry_sample_timestamp = sample.timestamp;

    const std::uint32_t sequence_no = ++g_system_state.telemetry_sequence_no;

    char topic[kOutgoingTopicMaxLength] = {};
    BuildTelemetryTopic(topic, sizeof(topic));

    char payload[kOutgoingPayloadMaxLength] = {};
    BuildTelemetryPayload(sample, sequence_no, false, payload, sizeof(payload));
    if (MqttPublish(topic, payload)) {
        return;
    }

    OutgoingMessage message = {};
    std::snprintf(message.topic, sizeof(message.topic), "%s", topic);
    BuildTelemetryPayload(sample, sequence_no, true, message.payload, sizeof(message.payload));
    message.buffered = true;
    EnqueueOutgoingMessage(message);
}
