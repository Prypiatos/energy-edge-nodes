#include "buffer_manager.h"
#include "command_manager.h"
#include "config.h"
#include "event_manager.h"
#include "globals.h"
#include "health_manager.h"
#include "mqtt_manager.h"
#include "sensor_manager.h"
#include "telemetry_manager.h"
#include "time_manager.h"
#include "wifi_manager.h"

// One-time initialization called at boot.
void setup() {
    // Initialize runtime configuration first (loads from EEPROM, etc).
    InitRuntimeConfig();

    // Initialize time manager for timestamp generation.
    InitTimeManager();

    // Initialize connectivity managers.
    InitWifiManager();
    InitMqttManager();

    // Initialize data acquisition and aggregation.
    InitSensorManager();
    InitTelemetryManager();
    InitHealthManager();

    // Initialize event and command processing.
    evnt_mngr::InitEventManager();
    InitCommandManager();

    // Initialize buffering layer.
    InitBufferManager();
}

// Main event loop called repeatedly by Arduino framework.
void loop() {
    // Sync time if needed (NTP or SNTP).
    SyncTimeIfNeeded();

    // Handle Wi-Fi and MQTT connectivity.
    RunWifiTask();
    RunMqttTask();

    // Acquire sensor readings and process input.
    RunSensorTask();

    // Generate telemetry and health reports.
    RunTelemetryTask();
    RunHealthTask();

    // Detect events and handle commands.
    evnt_mngr::RunEventTask();
    RunCommandTask();

    // Manage buffering of outgoing messages.
    RunBufferTask();
}
