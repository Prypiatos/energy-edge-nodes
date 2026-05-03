#include <unity.h>
#include <cstdio>
#include <cstring>
#include <cstdint>

#include "types.h"
#include "event_rules.h"

// ─────────────────────────────────────────
// Minimal globals — defined here for native
// ─────────────────────────────────────────
// struct SystemState {
//     bool wifi_connected;
//     bool mqtt_connected;
//     bool sensor_ok;
//     std::uint32_t uptime_sec;
//     std::uint32_t buffered_count;
//     char status[24];
// };

// struct RuntimeConfig {
//     std::uint32_t telemetry_interval_sec;
//     std::uint32_t health_interval_sec;
//     float current_warning_threshold;
//     float current_critical_threshold;
//     float power_spike_delta;
// };

static SystemState g_system_state = {};
static RuntimeConfig g_runtime_config = {};

// ─────────────────────────────────────────
// Stubs
// ─────────────────────────────────────────
static bool s_mqtt_publish_called = false;
static bool s_enqueue_called      = false;

static const char* GetNodeId()   { return "plug_01"; }
static const char* GetNodeType() { return "plug"; }

static void FormatTimestampISO8601(std::uint32_t, char* buf, std::size_t size) {
    std::snprintf(buf, size, "2026-01-01T00:00:00Z");
}

// ─────────────────────────────────────────
// Inline re-implementation of event_manager
// logic — mirrors src/event_manager.cpp but
// without Arduino/firmware dependencies
// ─────────────────────────────────────────
static constexpr std::uint32_t kDefaultEventCooldownSec    = 10;
static constexpr float         kDefaultCurrentWarning      = 8.0F;
static constexpr float         kDefaultPowerSpikeDelta     = 300.0F;
static constexpr char          kEventTopicTemplate[]       = "energy/nodes/%s/events";

enum class EventKind : std::size_t {
    PowerSpike      = 0,
    OverloadWarning = 1,
    PowerDown       = 2,
    Count,
};

static SensorSample  s_previous_sample                                          = {};
static bool          s_has_previous_sample                                      = false;
static bool          s_overload_active                                          = false;
static std::uint32_t s_last_event_timestamps[static_cast<std::size_t>(EventKind::Count)] = {};

static float GetWarningThreshold() {
    return g_runtime_config.current_warning_threshold > 0.0F
               ? g_runtime_config.current_warning_threshold
               : kDefaultCurrentWarning;
}

static float GetSpikeDelta() {
    return g_runtime_config.power_spike_delta > 0.0F
               ? g_runtime_config.power_spike_delta
               : kDefaultPowerSpikeDelta;
}

static bool IsWithinCooldown(EventKind kind, std::uint32_t ts) {
    const std::size_t   idx  = static_cast<std::size_t>(kind);
    const std::uint32_t last = s_last_event_timestamps[idx];
    if (last == 0)        return false;
    if (ts <= last)       return true;
    return (ts - last) < kDefaultEventCooldownSec;
}

static void MarkEmitted(EventKind kind, std::uint32_t ts) {
    s_last_event_timestamps[static_cast<std::size_t>(kind)] = ts;
}

// struct EventMessage {
//     char         event_type[32];
//     char         severity[16];
//     char         message[128];
//     std::uint32_t timestamp;
//     bool         buffered;
// };

static void BuildEvent(EventMessage* e,
                       const char*   event_type,
                       const char*   severity,
                       const char*   message,
                       std::uint32_t ts,
                       bool          buffered) {
    std::memset(e, 0, sizeof(EventMessage));
    std::snprintf(e->event_type, sizeof(e->event_type), "%s", event_type);
    std::snprintf(e->severity,   sizeof(e->severity),   "%s", severity);
    std::snprintf(e->message,    sizeof(e->message),    "%s", message);
    e->timestamp = ts;
    e->buffered  = buffered;
}

static bool TryPublish(const EventMessage&) {
    if (!g_system_state.wifi_connected || !g_system_state.mqtt_connected) {
        return false;
    }
    s_mqtt_publish_called = true;
    return true;
}

static void BufferEvent(const EventMessage&) {
    s_enqueue_called = true;
}

static void EmitEvent(EventKind     kind,
                      const char*   event_type,
                      const char*   severity,
                      const char*   message,
                      std::uint32_t ts) {
    if (IsWithinCooldown(kind, ts)) return;

    EventMessage event;
    BuildEvent(&event, event_type, severity, message, ts, false);
    if (!TryPublish(event)) {
        event.buffered = true;
        BufferEvent(event);
    }
    MarkEmitted(kind, ts);
}

static void InitEventManager() {
    std::memset(s_last_event_timestamps, 0, sizeof(s_last_event_timestamps));
    s_previous_sample     = SensorSample{};
    s_has_previous_sample = false;
    s_overload_active     = false;
}

static SensorSample s_latest_sample = {};

static void RunEventTask() {
    const SensorSample current = s_latest_sample;
    if (!current.valid) return;

    if (!s_has_previous_sample) {
        s_previous_sample     = current;
        s_has_previous_sample = true;
        s_overload_active     = current.current > GetWarningThreshold();
        return;
    }

    if (current.timestamp == s_previous_sample.timestamp) return;

    const EventEvaluation ev = EvaluateEventTransitions(
        s_previous_sample, current,
        GetWarningThreshold(), GetSpikeDelta(),
        s_overload_active);

    if (ev.emit_power_spike) {
        EmitEvent(EventKind::PowerSpike, "power_spike", "high",
                  "Power increased above configured spike delta", current.timestamp);
    }
    if (ev.emit_overload_warning) {
        EmitEvent(EventKind::OverloadWarning, "overload_warning", "high",
                  "Current exceeded warning threshold", current.timestamp);
    }
    s_overload_active = ev.overload_active;

    if (ev.emit_power_down) {
        EmitEvent(EventKind::PowerDown, "power_down", "medium",
                  "Load dropped to zero from active state", current.timestamp);
    }

    s_previous_sample = current;
}

// ─────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────
static SensorSample BuildSample(std::uint32_t ts, float current, float power, bool valid = true) {
    SensorSample s = {};
    s.timestamp = ts;
    s.current   = current;
    s.power     = power;
    s.valid     = valid;
    return s;
}

static void SetConnected() {
    g_system_state.wifi_connected = true;
    g_system_state.mqtt_connected = true;
}

static void ResetState() {
    g_system_state  = SystemState{};
    g_runtime_config = RuntimeConfig{};
    s_latest_sample  = SensorSample{};
    s_mqtt_publish_called = false;
    s_enqueue_called      = false;
    InitEventManager();
}

// ─────────────────────────────────────────
// Tests
// ─────────────────────────────────────────
void test_first_valid_sample_does_not_emit_event() {
    ResetState();
    SetConnected();
    s_latest_sample = BuildSample(1, 9.0F, 500.0F);
    RunEventTask();
    TEST_ASSERT_FALSE(s_mqtt_publish_called);
}

void test_invalid_sample_does_not_emit_event() {
    ResetState();
    SetConnected();
    s_latest_sample = BuildSample(1, 0.0F, 0.0F, false);
    RunEventTask();
    TEST_ASSERT_FALSE(s_mqtt_publish_called);
}

void test_power_spike_emits_event() {
    ResetState();
    SetConnected();
    s_latest_sample = BuildSample(1, 1.0F, 100.0F);
    RunEventTask();
    s_mqtt_publish_called = false;
    s_latest_sample = BuildSample(2, 1.2F, 450.0F);
    RunEventTask();
    TEST_ASSERT_TRUE(s_mqtt_publish_called);
}

void test_overload_warning_emits_event() {
    ResetState();
    SetConnected();
    s_latest_sample = BuildSample(1, 7.0F, 200.0F);
    RunEventTask();
    s_mqtt_publish_called = false;
    s_latest_sample = BuildSample(2, 9.0F, 220.0F);
    RunEventTask();
    TEST_ASSERT_TRUE(s_mqtt_publish_called);
}

void test_power_down_emits_event() {
    ResetState();
    SetConnected();
    s_latest_sample = BuildSample(1, 1.4F, 320.0F);
    RunEventTask();
    s_mqtt_publish_called = false;
    s_latest_sample = BuildSample(2, 0.0F, 0.0F);
    RunEventTask();
    TEST_ASSERT_TRUE(s_mqtt_publish_called);
}

void test_same_timestamp_does_not_reprocess() {
    ResetState();
    SetConnected();
    s_latest_sample = BuildSample(1, 1.0F, 100.0F);
    RunEventTask();
    s_latest_sample = BuildSample(2, 1.2F, 450.0F);
    RunEventTask();
    s_mqtt_publish_called = false;
    RunEventTask();  // same timestamp — should skip
    TEST_ASSERT_FALSE(s_mqtt_publish_called);
}

void test_event_buffered_when_mqtt_disconnected() {
    ResetState();
    g_system_state.wifi_connected = false;
    g_system_state.mqtt_connected = false;
    s_latest_sample = BuildSample(1, 1.0F, 100.0F);
    RunEventTask();
    s_latest_sample = BuildSample(2, 1.2F, 450.0F);
    RunEventTask();
    TEST_ASSERT_FALSE(s_mqtt_publish_called);
    TEST_ASSERT_TRUE(s_enqueue_called);
}

void test_cooldown_suppresses_duplicate_event() {
    ResetState();
    SetConnected();
    s_latest_sample = BuildSample(1, 1.0F, 100.0F);
    RunEventTask();
    s_latest_sample = BuildSample(2, 1.2F, 450.0F);
    RunEventTask();
    s_mqtt_publish_called = false;
    s_latest_sample = BuildSample(3, 1.2F, 800.0F);  // within cooldown
    RunEventTask();
    TEST_ASSERT_FALSE(s_mqtt_publish_called);
}

// ─────────────────────────────────────────
// Unity required
// ─────────────────────────────────────────
void setUp()    { ResetState(); }
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_first_valid_sample_does_not_emit_event);
    RUN_TEST(test_invalid_sample_does_not_emit_event);
    RUN_TEST(test_power_spike_emits_event);
    RUN_TEST(test_overload_warning_emits_event);
    RUN_TEST(test_power_down_emits_event);
    RUN_TEST(test_same_timestamp_does_not_reprocess);
    RUN_TEST(test_event_buffered_when_mqtt_disconnected);
    RUN_TEST(test_cooldown_suppresses_duplicate_event);
    return UNITY_END();
}