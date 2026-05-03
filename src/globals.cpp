#include <Arduino.h>

#include "config.h"
#include "globals.h"

#include <cstdio>

SystemState g_system_state = {};
RuntimeConfig g_runtime_config = {};
SensorSample g_latest_sample = {};

namespace {

void CopyStatus(const char* status) {
    std::snprintf(g_system_state.status, sizeof(g_system_state.status), "%s", status);
}

}  // namespace

void InitSystemState() {
    g_system_state = SystemState{};
    g_latest_sample = SensorSample{};
    CopyStatus("offline_intended");
}

void UpdateSystemStatus() {
    if (!HasNodeIdentity() || !HasWifiCredentials()) {
        CopyStatus("offline_intended");
        return;
    }

    if (g_system_state.wifi_connected && g_system_state.mqtt_connected && g_system_state.sensor_ok) {
        CopyStatus("online");
        return;
    }

    CopyStatus("degraded");
}

void RefreshSystemState() {
    g_system_state.uptime_sec = millis() / 1000U;
    UpdateSystemStatus();
}
