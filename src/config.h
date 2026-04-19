#pragma once

#include "types.h"

constexpr unsigned int kSensorSampleIntervalSec = 1;
constexpr unsigned int kTelemetryPublishIntervalSec = 2;
constexpr unsigned int kHealthPublishIntervalSec = 30;
constexpr unsigned int kWifiRetryIntervalSec = 10;

constexpr float kDefaultCurrentWarningThreshold = 8.0F;
constexpr float kDefaultCurrentCriticalThreshold = 10.0F;
constexpr float kDefaultPowerSpikeDelta = 300.0F;

constexpr char kFlashConfigPath[] = "/config.json";

constexpr char kDefaultNodeId[] = "plug_01";
constexpr char kDefaultNodeType[] = "plug";

RuntimeConfig GetDefaultRuntimeConfig();
bool InitRuntimeConfig(const char* config_path = kFlashConfigPath);
