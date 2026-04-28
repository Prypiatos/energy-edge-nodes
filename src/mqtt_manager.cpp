#include "mqtt_manager.h"

#include "globals.h"

void InitMqttManager() {}

void RunMqttTask() {}

// Publishes a message to the given topic via the active MQTT connection.
// Returns true only when the broker accepted the message.
bool MqttPublish(const char* topic, const char* payload) {
	if (topic == nullptr || topic[0] == '\0' || payload == nullptr || payload[0] == '\0') {
		return false;
	}

	if (!g_system_state.wifi_connected || !g_system_state.mqtt_connected) {
		return false;
	}

	// TODO: Integrate real MQTT client publish call here (e.g. PubSubClient::publish).
	return false;
}
