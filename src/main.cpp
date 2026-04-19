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
    InitRuntimeConfig();
    InitMqttManager();
}

void loop() {
    RunMqttTask();
}