#include "event_manager.h"

#include "buffer_manager.h"
#include "config.h"
#include "globals.h"
#include "mqtt_manager.h"

#include <cstdio>
#include <cstring>
#include <ctime>

// Operational thresholds and cooldown for event generation.
static constexpr std::uint32_t kDefaultEventCooldownSec = 10;
static constexpr float kLoadActiveThreshold = 0.05F;
static constexpr float kLoadZeroThreshold = 0.01F;

// MQTT topic template used for publishing node event messages.
static constexpr char kEventTopicTemplate[] = "energy/nodes/%s/events";

// Internal event categories used for cooldown bookkeeping.
enum class EventKind : std::size_t {
	PowerSpike = 0,
	OverloadWarning = 1,
	PowerDown = 2,
	Count,
};

// Module-private state; internal linkage prevents accidental access from other TUs.
static SensorSample g_previous_sample = {};
static bool g_has_previous_sample = false;
static bool g_overload_active = false;

// Buffer size for ISO 8601 UTC timestamp strings (e.g. "2024-01-01T00:00:00Z" + NUL).
static constexpr std::size_t kISO8601TimestampSize = 32;

// Per-event last-emitted timestamps used to apply cooldown.
static std::uint32_t g_last_event_timestamps[static_cast<std::size_t>(EventKind::Count)] = {};

// Uses runtime override when present; otherwise falls back to compile-time default.
static float GetWarningCurrentThreshold() {
	if (g_runtime_config.current_warning_threshold > 0.0F) {
		return g_runtime_config.current_warning_threshold;
	}

	return kDefaultCurrentWarningThreshold;
}

// Uses runtime override when present; otherwise falls back to compile-time default.
static float GetPowerSpikeDeltaThreshold() {
	if (g_runtime_config.power_spike_delta > 0.0F) {
		return g_runtime_config.power_spike_delta;
	}

	return kDefaultPowerSpikeDelta;
}

// Returns true when the event kind is still inside its suppression window.
static bool IsWithinCooldown(EventKind kind, std::uint32_t timestamp_sec) {
	const std::size_t index = static_cast<std::size_t>(kind);
	const std::uint32_t last_timestamp = g_last_event_timestamps[index];
	if (last_timestamp == 0) {
		return false;
	}

	// Guard against clock regressions (e.g. NTP sync moving time backwards).
	// Treat any non-advancing or reversed timestamp as still within cooldown.
	if (timestamp_sec <= last_timestamp) {
		return true;
	}

	// Suppress duplicate event type bursts within a fixed cooldown window.
	return (timestamp_sec - last_timestamp) < kDefaultEventCooldownSec;
}

// Records the latest emission time for cooldown checks.
static void MarkEventEmitted(EventKind kind, std::uint32_t timestamp_sec) {
	g_last_event_timestamps[static_cast<std::size_t>(kind)] = timestamp_sec;
}

// Formats a Unix epoch timestamp as an ISO 8601 UTC string (e.g. "2024-01-01T00:00:00Z").
static void FormatTimestampISO8601(std::uint32_t timestamp, char* buf, std::size_t buf_size) {
	if (buf == nullptr || buf_size == 0) {
		return;
	}

	const time_t t = static_cast<time_t>(timestamp);
	struct tm tm_info = {};
	gmtime_r(&t, &tm_info);
	strftime(buf, buf_size, "%Y-%m-%dT%H:%M:%SZ", &tm_info);
}

// Builds the MQTT event topic for the configured node identity.
static void BuildTopic(char* topic, std::size_t topic_size) {
	if (topic == nullptr || topic_size == 0) {
		return;
	}

	std::snprintf(topic, topic_size, kEventTopicTemplate, GetNodeId());
}

// Serializes a single EventMessage as compact JSON with an ISO 8601 timestamp.
static void BuildEventPayload(const EventMessage& event, char* payload, std::size_t payload_size) {
	if (payload == nullptr || payload_size == 0) {
		return;
	}

	char timestamp_str[kISO8601TimestampSize] = {};
	FormatTimestampISO8601(event.timestamp, timestamp_str, sizeof(timestamp_str));

	std::snprintf(payload,
				  payload_size,
				  "{\"node_id\":\"%s\",\"node_type\":\"%s\",\"timestamp\":\"%s\","
				  "\"event_type\":\"%s\",\"severity\":\"%s\",\"message\":\"%s\","
				  "\"buffered\":%s}",
				  GetNodeId(),
				  GetNodeType(),
				  timestamp_str,
				  event.event_type,
				  event.severity,
				  event.message,
				  event.buffered ? "true" : "false");
}

// Attempts to publish one event only when connectivity prerequisites are met.
// Returns true when the MQTT broker accepted the message.
static bool TryPublishEvent(const EventMessage& event) {
	if (!g_system_state.wifi_connected || !g_system_state.mqtt_connected) {
		return false;
	}

	char topic[128] = {};
	BuildTopic(topic, sizeof(topic));

	char payload[512] = {};
	BuildEventPayload(event, payload, sizeof(payload));

	return MqttPublish(topic, payload);
}

// Routes a failed publish to the shared buffer manager for later retry.
static void BufferEvent(const EventMessage& event) {
	OutgoingMessage msg = {};
	BuildTopic(msg.topic, sizeof(msg.topic));
	BuildEventPayload(event, msg.payload, sizeof(msg.payload));
	msg.buffered = true;
	EnqueueOutgoingMessage(msg);
}

// Initializes a fixed-size EventMessage safely from provided fields.
static void BuildEvent(EventMessage* event,
					   const char* event_type,
					   const char* severity,
					   const char* message,
					   std::uint32_t timestamp,
					   bool buffered) {
	if (event == nullptr) {
		return;
	}

	std::memset(event, 0, sizeof(EventMessage));
	std::snprintf(event->event_type, sizeof(event->event_type), "%s", event_type);
	std::snprintf(event->severity, sizeof(event->severity), "%s", severity);
	std::snprintf(event->message, sizeof(event->message), "%s", message);
	event->timestamp = timestamp;
	event->buffered = buffered;
}

// Emits an event with cooldown enforcement; routes to buffer on publish failure.
static void EmitEvent(EventKind kind,
					  const char* event_type,
					  const char* severity,
					  const char* message,
					  std::uint32_t timestamp) {
	if (IsWithinCooldown(kind, timestamp)) {
		return;
	}

	EventMessage event;
	BuildEvent(&event, event_type, severity, message, timestamp, false);
	if (!TryPublishEvent(event)) {
		event.buffered = true;
		BufferEvent(event);
	}

	MarkEventEmitted(kind, timestamp);
}

// Treats load as active if either current or power rises above active threshold.
static bool IsLoadActive(const SensorSample& sample) {
	return sample.current > kLoadActiveThreshold || sample.power > kLoadActiveThreshold;
}

// Treats load as zero only when both current and power are below zero threshold.
static bool IsLoadZero(const SensorSample& sample) {
	return sample.current <= kLoadZeroThreshold && sample.power <= kLoadZeroThreshold;
}

// Public API
void InitEventManager() {
	std::memset(g_last_event_timestamps, 0, sizeof(g_last_event_timestamps));

	g_previous_sample = SensorSample{};
	g_has_previous_sample = false;
	g_overload_active = false;
}

void RunEventTask() {
	const SensorSample current = g_latest_sample;
	if (!current.valid) {
		return;
	}

	if (!g_has_previous_sample) {
		// First valid sample is used as baseline for transition/delta detection.
		g_previous_sample = current;
		g_has_previous_sample = true;
		g_overload_active = current.current > GetWarningCurrentThreshold();
		return;
	}

	// Skip re-processing if sensor data did not advance.
	if (current.timestamp == g_previous_sample.timestamp) {
		return;
	}

	// Detect sudden positive power changes.
	const float power_delta = current.power - g_previous_sample.power;
	if (power_delta > GetPowerSpikeDeltaThreshold()) {
		EmitEvent(EventKind::PowerSpike,
				  "power_spike",
				  "high",
				  "Power increased above configured spike delta",
				  current.timestamp);
	}

	// Rising-edge detection for overload warning.
	const bool overload_now = current.current > GetWarningCurrentThreshold();
	if (overload_now && !g_overload_active) {
		EmitEvent(EventKind::OverloadWarning,
				  "overload_warning",
				  "high",
				  "Current exceeded warning threshold",
				  current.timestamp);
	}
	g_overload_active = overload_now;

	// Detect active-to-zero transition as a power-down event.
	if (IsLoadActive(g_previous_sample) && IsLoadZero(current)) {
		EmitEvent(EventKind::PowerDown,
				  "power_down",
				  "medium",
				  "Load dropped to zero from active state",
				  current.timestamp);
	}

	g_previous_sample = current;
}
