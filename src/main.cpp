#include <Arduino.h>

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
    Serial.begin(115200);

    // Initialize runtime configuration first (loads from flash, falls back to defaults).
    if (!InitRuntimeConfig()) {
        Serial.println("Config load failed, using defaults");
    }

    // Initialize time manager for timestamp generation.
    InitTimeManager();

    // Initialize connectivity managers. RunWifiTask spawns a FreeRTOS task.
    InitWifiManager();
    RunWifiTask();
    InitMqttManager();

    // Initialize data acquisition and aggregation.
    InitSensorManager();
    InitTelemetryManager();
    InitHealthManager();

    // Initialize event and command processing.
    InitEventManager();
    InitCommandManager();

    // Initialize buffering layer.
    InitBufferManager();
}

// Main event loop called repeatedly by Arduino framework.
// FreeRTOS tasks (e.g. Wi-Fi) run independently; other managers are polled here.
void loop() {
    // Sync time if needed (NTP or SNTP).
    SyncTimeIfNeeded();

    // Handle MQTT connectivity.
    RunMqttTask();

    // Acquire sensor readings and process input.
    RunSensorTask();

    // Generate telemetry and health reports.
    RunTelemetryTask();
    RunHealthTask();

    // Detect events and handle commands.
    RunEventTask();
    RunCommandTask();

    // Flush buffered messages when connectivity is available.
    RunBufferTask();

    vTaskDelay(pdMS_TO_TICKS(1000));
}