// telemetry_manager.cpp
// Owner: Yohan
// Responsibility:
//   - Every 2 seconds, read the latest valid sensor sample from g_latest_sample
//   - Build the JSON telemetry payload
//   - Try to publish via MqttPublish() (Damindu's interface)
//   - On publish failure, hand the message to EnqueueOutgoingMessage() (Damindu's buffer)
//   - Called from loop() in main.cpp via RunTelemetryTask()

#include "telemetry_manager.h"

#include "buffer_manager.h"
#include "config.h"
#include "globals.h"
#include "mqtt_manager.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

// MQTT topic template — matches the agreed interface spec
static constexpr char kTelemetryTopicTemplate[] = "energy/nodes/%s/telemetry";

// Sequence number incremented on every published telemetry message
static std::uint32_t g_telemetry_sequence_no = 0;

// Tracks when we last published (milliseconds)
static unsigned long g_last_publish_ms = 0;


// Buffer sizes
static constexpr std::size_t kTopicBufferSize   = 128;
static constexpr std::size_t kPayloadBufferSize = 512;

// ─────────────────────────────────────────────────────────────────────────────
// InitTelemetryManager
// Called once from setup() in main.cpp
// ─────────────────────────────────────────────────────────────────────────────
void InitTelemetryManager() {
    g_telemetry_sequence_no = 0;
    g_last_publish_ms       = 0;
    Serial.println("[telemetry] Telemetry manager initialised");
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildTelemetryPayload (private helper)
// Fills payload buffer with a JSON string matching the agreed external contract:
//   node_id, timestamp, voltage, current, power, energy_wh
// ─────────────────────────────────────────────────────────────────────────────
static void BuildTelemetryPayload(const SensorSample& sample,
                                   char* payload,
                                   std::size_t payload_size) {
    std::snprintf(payload, payload_size,
        "{"
        "\"node_id\":\"%s\","
        "\"timestamp\":%llu,"
        "\"voltage\":%.1f,"
        "\"current\":%.2f,"
        "\"power\":%.1f,"
        "\"energy_wh\":%.1f"
        "}",
        GetNodeId(),
        static_cast<unsigned long long>(sample.timestamp),
        sample.voltage,
        sample.current,
        sample.power,
        sample.energy_wh
    );
}

// ─────────────────────────────────────────────────────────────────────────────
// RunTelemetryTask
// Called every loop() iteration from main.cpp.
// Uses elapsed-time gating to publish every telemetry_interval_sec seconds.
// ─────────────────────────────────────────────────────────────────────────────
void RunTelemetryTask() {
    const unsigned long now_ms = millis();

    // Only publish when the interval has elapsed
    if (now_ms - g_last_publish_ms < (g_runtime_config.telemetry_interval_sec * 1000UL)) {
        return;
    }
    g_last_publish_ms = now_ms;

    // Skip if sensor has no valid reading yet
    if (!g_latest_sample.valid) {
        Serial.println("[telemetry] No valid sample yet, skipping");
        return;
    }

    // Build topic and payload
    char topic[kTopicSize];
    std::snprintf(topic, sizeof(topic), kTelemetryTopicTemplate, GetNodeId());

  char payload[kPayloadSize];
    BuildTelemetryPayload(g_latest_sample, payload, sizeof(payload));

    // Try to publish via Damindu's MQTT manager
    const bool published = MqttPublish(topic, payload);

    if (published) {
        g_telemetry_sequence_no++;
        Serial.printf("[telemetry] Published seq=%lu\n",
                      static_cast<unsigned long>(g_telemetry_sequence_no));
    } else {
        // Publish failed — hand to Damindu's buffer manager for retry
        Serial.println("[telemetry] Publish failed, buffering");

        OutgoingMessage msg = {};
        std::strncpy(msg.topic,   topic,   sizeof(msg.topic)   - 1);
        std::strncpy(msg.payload, payload, sizeof(msg.payload) - 1);
        msg.buffered = true;

        EnqueueOutgoingMessage(msg);

        // Still increment sequence so numbers don't repeat on retry
        g_telemetry_sequence_no++;
    }
}
