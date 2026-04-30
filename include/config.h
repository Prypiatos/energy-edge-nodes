#pragma once

#include <cstddef>

#include "types.h"

constexpr unsigned int kSensorSampleIntervalSec = 1;
constexpr unsigned int kTelemetryPublishIntervalSec = 2;
constexpr unsigned int kHealthPublishIntervalSec = 30;
constexpr unsigned int kWifiRetryIntervalSec = 10;

constexpr float kDefaultCurrentWarningThreshold = 8.0F;
constexpr float kDefaultCurrentCriticalThreshold = 10.0F;
constexpr float kDefaultPowerSpikeDelta = 300.0F;
constexpr std::uint16_t kDefaultMqttPort = 1883;

constexpr char kFlashConfigPath[] = "/config.json";

constexpr char kDefaultNodeId[] = "unprovisioned";
constexpr char kDefaultNodeType[] = "plug";

RuntimeConfig GetDefaultRuntimeConfig();
bool InitRuntimeConfig(const char* config_path = kFlashConfigPath);
bool SaveRuntimeConfig(const RuntimeConfig& config, const char* config_path = kFlashConfigPath);
void BuildRuntimeConfigJson(const RuntimeConfig& config, char* buffer, std::size_t buffer_size);

const char* GetNodeId();
const char* GetNodeType();
bool HasNodeIdentity();
bool HasWifiCredentials();
bool HasMqttBrokerConfig();

constexpr int kWifiMaxRetryCount = 50;
constexpr int kWifiShortDelayMs = 200;
constexpr int kWifiRetryBackoffMs = 2000;
constexpr int kWifiConnectedCheckMs = 5000;
