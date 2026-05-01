// health_manager.cpp
// Owner: Yohan
// Responsibility:
//   - Every 30 seconds, read the shared g_system_state from globals
//   - Derive node status: "online", "degraded", or "offline_intended"
//   - Build the JSON health payload
//   - Try to publish via MqttPublish() (Damindu's interface)
//   - On publish failure, hand to EnqueueOutgoingMessage() (Damindu's buffer)
//   - Called from loop() in main.cpp via RunHealthTask()

#include "health_manager.h"

#include "buffer_manager.h"
#include "config.h"
#include "globals.h"
#include "mqtt_manager.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

// MQTT topic template — matches the agreed interface spec
static constexpr char kHealthTopicTemplate[] = "energy/nodes/%s/health";

// Sequence number incremented on every published health message
static std::uint32_t g_health_sequence_no = 0;

// Tracks when we last published (milliseconds)
static unsigned long g_last_publish_ms = 0;

// Buffer sizes
static constexpr std::size_t kTopicBufferSize   = 128;
static constexpr std::size_t kPayloadBufferSize = 512;

// ─────────────────────────────────────────────────────────────────────────────
// InitHealthManager
// Called once from setup() in main.cpp
// ─────────────────────────────────────────────────────────────────────────────
void InitHealthManager() {
    g_health_sequence_no = 0;
    g_last_publish_ms    = 0;
    Serial.println("[health] Health manager initialised");
}

// ─────────────────────────────────────────────────────────────────────────────
// DeriveStatus (private helper)
// Returns status string based on subsystem health flags in SystemState:
//   online   — Wi-Fi, MQTT, and sensor are all healthy
//   degraded — node is running but at least one subsystem is unhealthy
// "offline_intended" is set externally (e.g. by command handler) — not derived here
// ─────────────────────────────────────────────────────────────────────────────
static const char* DeriveStatus(const SystemState& state) {
    if (state.wifi_connected && state.mqtt_connected && state.sensor_ok) {
        return "online";
    }
    return "degraded";
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildHealthPayload (private helper)
// Fills payload buffer with JSON matching the agreed health contract:
//   node_id, node_type, timestamp, sequence_no, status, uptime_sec,
//   mqtt_connected, wifi_connected, sensor_ok, buffered_count
// ─────────────────────────────────────────────────────────────────────────────
static void BuildHealthPayload(const SystemState& state,
                                std::uint32_t sequence_no,
                                const char* status,
                                char* payload,
                                std::size_t payload_size) {
    std::snprintf(payload, payload_size,
        "{"
        "\"node_id\":\"%s\","
        "\"node_type\":\"%s\","
        "\"timestamp\":%lu,"
        "\"sequence_no\":%lu,"
        "\"status\":\"%s\","
        "\"uptime_sec\":%lu,"
        "\"mqtt_connected\":%s,"
        "\"wifi_connected\":%s,"
        "\"sensor_ok\":%s,"
        "\"buffered_count\":%lu"
        "}",
        kDefaultNodeId,
        kDefaultNodeType,
        static_cast<unsigned long>(state.uptime_sec),
        static_cast<unsigned long>(sequence_no),
        status,
        static_cast<unsigned long>(state.uptime_sec),
        state.mqtt_connected ? "true" : "false",
        state.wifi_connected ? "true" : "false",
        state.sensor_ok      ? "true" : "false",
        static_cast<unsigned long>(state.buffered_count)
    );
}

// ─────────────────────────────────────────────────────────────────────────────
// RunHealthTask
// Called every loop() iteration from main.cpp.
// Uses elapsed-time gating to publish every kHealthPublishIntervalSec seconds.
// Also keeps uptime_sec updated in g_system_state.
// ─────────────────────────────────────────────────────────────────────────────
void RunHealthTask() {
    // Always keep uptime updated regardless of publish interval
    g_system_state.uptime_sec = static_cast<std::uint32_t>(millis() / 1000UL);

    const unsigned long now_ms = millis();

    // Only publish when the interval has elapsed
    if (now_ms - g_last_publish_ms < (g_runtime_config.health_interval_sec * 1000UL)) {
        return;
    }
    g_last_publish_ms = now_ms;

    // Take a snapshot of current state to build the payload
    const SystemState snapshot = g_system_state;
    const char* status         = DeriveStatus(snapshot);

    // Build topic and payload
    char topic[kTopicBufferSize];
    std::snprintf(topic, sizeof(topic), kHealthTopicTemplate, kDefaultNodeId);

    char payload[kPayloadBufferSize];
    BuildHealthPayload(snapshot, g_health_sequence_no, status,
                       payload, sizeof(payload));

    // Try to publish via Damindu's MQTT manager
    const bool published = MqttPublish(topic, payload);

    if (published) {
        g_health_sequence_no++;
        Serial.printf("[health] Published seq=%lu status=%s uptime=%lus\n",
                      static_cast<unsigned long>(g_health_sequence_no),
                      status,
                      static_cast<unsigned long>(snapshot.uptime_sec));
    } else {
        // Publish failed — hand to Damindu's buffer manager for retry
        Serial.println("[health] Publish failed, buffering");

        OutgoingMessage msg = {};
        std::strncpy(msg.topic,   topic,   sizeof(msg.topic)   - 1);
        std::strncpy(msg.payload, payload, sizeof(msg.payload) - 1);
        msg.buffered = true;

        EnqueueOutgoingMessage(msg);
        g_health_sequence_no++;
    }
}