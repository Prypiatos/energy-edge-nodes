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

void setup() {
    Serial.begin(115200);

    // Initialize config
    if (!InitRuntimeConfig()) {
    Serial.println("Config load failed, using defaults");
    }

    // Initialize Wi-Fi
    InitWifiManager();
    RunWifiTask();

    // (Later you will add more tasks here)
    // RunMqttTask();
    // RunSensorTask();
    // etc.
}

void loop() {
    // Keep loop alive (FreeRTOS tasks handle everything)
    vTaskDelay(pdMS_TO_TICKS(1000));
}