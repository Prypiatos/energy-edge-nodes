#include "event_manager.h"

#include "config.h"
#include "globals.h"

#include <cstdio>
#include <cstring>

namespace evnt_mngr {



constexpr std::size_t kEventQueueCapacity = 16;
constexpr std::uint32_t kDefaultEventCooldownSec = 10;
constexpr float kLoadActiveThreshold = 0.05F;
constexpr float kLoadZeroThreshold = 0.01F;

constexpr char kEventTopicTemplate[] = "energy/nodes/%s/events";

enum class EventKind : std::size_t {
	PowerSpike = 0,
	OverloadWarning = 1,
	PowerDown = 2,
	Count,
};

EventMessage g_pending_events[kEventQueueCapacity] = {};
std::size_t g_pending_count = 0;

SensorSample g_previous_sample = {};
bool g_has_previous_sample = false;
bool g_overload_active = false;

std::uint32_t g_last_event_timestamps[static_cast<std::size_t>(EventKind::Count)] = {};

float GetWarningCurrentThreshold() {
	if (g_runtime_config.current_warning_threshold > 0.0F) {
		return g_runtime_config.current_warning_threshold;
	}

	return kDefaultCurrentWarningThreshold;
}

float GetPowerSpikeDeltaThreshold() {
	if (g_runtime_config.power_spike_delta > 0.0F) {
		return g_runtime_config.power_spike_delta;
	}

	return kDefaultPowerSpikeDelta;
}

bool IsWithinCooldown(EventKind kind, std::uint32_t timestamp_sec) {
	const std::size_t index = static_cast<std::size_t>(kind);
	const std::uint32_t last_timestamp = g_last_event_timestamps[index];
	if (last_timestamp == 0) {
		return false;
	}

	return (timestamp_sec - last_timestamp) < kDefaultEventCooldownSec;
}

void MarkEventEmitted(EventKind kind, std::uint32_t timestamp_sec) {
	g_last_event_timestamps[static_cast<std::size_t>(kind)] = timestamp_sec;
}

void UpdateBufferedCount() {
	g_system_state.buffered_count = static_cast<std::uint32_t>(g_pending_count);
}

void BuildTopic(char* topic, std::size_t topic_size) {
	if (topic == nullptr || topic_size == 0) {
		return;
	}

	std::snprintf(topic, topic_size, kEventTopicTemplate, kDefaultNodeId);
}

void BuildEventPayload(const EventMessage& event, char* payload, std::size_t payload_size) {
	if (payload == nullptr || payload_size == 0) {
		return;
	}

	std::snprintf(payload,
				  payload_size,
				  "{\"node_id\":\"%s\",\"node_type\":\"%s\",\"timestamp\":%lu,"
				  "\"event_type\":\"%s\",\"severity\":\"%s\",\"message\":\"%s\","
				  "\"buffered\":%s}",
				  kDefaultNodeId,
				  kDefaultNodeType,
				  static_cast<unsigned long>(event.timestamp),
				  event.event_type,
				  event.severity,
				  event.message,
				  event.buffered ? "true" : "false");
}

bool TryPublishEvent(const EventMessage& event) {
	if (!g_system_state.wifi_connected || !g_system_state.mqtt_connected) {
		return false;
	}

	char topic[128];
	BuildTopic(topic, sizeof(topic));

	char payload[512];
	BuildEventPayload(event, payload, sizeof(payload));

	return std::strlen(topic) > 0 && std::strlen(payload) > 0;
}

bool QueueEventForRetry(const EventMessage& event) {
	if (g_pending_count >= kEventQueueCapacity) {
		for (std::size_t index = 1; index < g_pending_count; ++index) {
			g_pending_events[index - 1] = g_pending_events[index];
		}

		g_pending_count = kEventQueueCapacity - 1;
	}

	g_pending_events[g_pending_count] = event;
	++g_pending_count;
	UpdateBufferedCount();
	return true;
}

void FlushPendingEvents() {
	if (g_pending_count == 0) {
		return;
	}

	while (g_pending_count > 0) {
		if (!TryPublishEvent(g_pending_events[0])) {
			break;
		}

		for (std::size_t index = 1; index < g_pending_count; ++index) {
			g_pending_events[index - 1] = g_pending_events[index];
		}

		--g_pending_count;
	}

	UpdateBufferedCount();
}

void BuildEvent(EventMessage* event,
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

void EmitEvent(EventKind kind,
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
		QueueEventForRetry(event);
	}

	MarkEventEmitted(kind, timestamp);
}

bool IsLoadActive(const SensorSample& sample) {
	return sample.current > kLoadActiveThreshold || sample.power > kLoadActiveThreshold;
}

bool IsLoadZero(const SensorSample& sample) {
	return sample.current <= kLoadZeroThreshold && sample.power <= kLoadZeroThreshold;
}


void InitEventManager() {
	g_pending_count = 0;
	std::memset(g_pending_events, 0, sizeof(g_pending_events));
	std::memset(g_last_event_timestamps, 0, sizeof(g_last_event_timestamps));

	g_previous_sample = SensorSample{};
	g_has_previous_sample = false;
	g_overload_active = false;

	UpdateBufferedCount();
}

void RunEventTask() {
	FlushPendingEvents();

	const SensorSample current = g_latest_sample;
	if (!current.valid) {
		return;
	}

	if (!g_has_previous_sample) {
		g_previous_sample = current;
		g_has_previous_sample = true;
		g_overload_active = current.current > GetWarningCurrentThreshold();
		return;
	}

	if (current.timestamp == g_previous_sample.timestamp) {
		return;
	}

	const float power_delta = current.power - g_previous_sample.power;
	if (power_delta > GetPowerSpikeDeltaThreshold()) {
		EmitEvent(EventKind::PowerSpike,
				  "power_spike",
				  "high",
				  "Power increased above configured spike delta",
				  current.timestamp);
	}

	const bool overload_now = current.current > GetWarningCurrentThreshold();
	if (overload_now && !g_overload_active) {
		EmitEvent(EventKind::OverloadWarning,
				  "overload_warning",
				  "high",
				  "Current exceeded warning threshold",
				  current.timestamp);
	}
	g_overload_active = overload_now;

	if (IsLoadActive(g_previous_sample) && IsLoadZero(current)) {
		EmitEvent(EventKind::PowerDown,
				  "power_down",
				  "medium",
				  "Load dropped to zero from active state",
				  current.timestamp);
	}

	g_previous_sample = current;
}

}  // namespace evnt_mngr
