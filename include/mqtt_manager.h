#pragma once

#include <cstddef>

void InitMqttManager();
void RunMqttTask();
bool MqttPublish(const char* topic, const char* payload);
