#pragma once

#include "types.h"

extern SystemState g_system_state;
extern RuntimeConfig g_runtime_config;
extern SensorSample g_latest_sample;

void InitSystemState();
void RefreshSystemState();
void UpdateSystemStatus();
