#pragma once

#include <cstddef>

void InitMqttManager();
void RunMqttTask();
bool MqttPublish(const char* topic, const char* payload);
bool ConsumePendingMqttCommand(char* topic,
                               std::size_t topic_size,
                               char* payload,
                               std::size_t payload_size);
