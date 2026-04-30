#pragma once

#include <cstddef>
#include <cstdint>

constexpr std::size_t kNodeIdMaxLength = 32;
constexpr std::size_t kNodeTypeMaxLength = 16;
constexpr std::size_t kWifiSsidMaxLength = 64;
constexpr std::size_t kWifiPasswordMaxLength = 64;

struct SensorSample {
    std::uint32_t timestamp;
    float voltage;
    float current;
    float power;
    float energy_wh;
    float frequency_hz;
    float power_factor;
    bool valid;
};

struct EventMessage {
    char event_type[32];
    char severity[16];
    char message[128];
    std::uint32_t timestamp;
    bool buffered;
};

struct OutgoingMessage {
    char topic[128];
    char payload[512];
    bool buffered;
};

struct RuntimeConfig {
    char node_id[kNodeIdMaxLength];
    char node_type[kNodeTypeMaxLength];
    char wifi_ssid[kWifiSsidMaxLength];
    char wifi_password[kWifiPasswordMaxLength];
    std::uint32_t telemetry_interval_sec;
    std::uint32_t health_interval_sec;
    float current_warning_threshold;
    float current_critical_threshold;
    float power_spike_delta;
};

struct SystemState {
    bool wifi_connected;
    bool mqtt_connected;
    bool sensor_ok;
    std::uint32_t uptime_sec;
    std::uint32_t telemetry_sequence_no;
    std::uint32_t health_sequence_no;
    std::uint32_t buffered_count;
    char status[24];
};
