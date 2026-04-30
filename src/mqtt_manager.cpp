#include "mqtt_manager.h"

#include <Arduino.h>
#include <WiFi.h>

#include <PubSubClient.h>

#include "config.h"
#include "globals.h"

#include <cstdio>
#include <cstring>

namespace {

constexpr unsigned long kMqttRetryIntervalMs = 5000;
constexpr std::uint16_t kMqttKeepAliveSec = 30;
constexpr std::size_t kCommandTopicBufferSize = 128;

constexpr char kCommandGetStatusTopicTemplate[] = "energy/nodes/%s/cmd/get_status";
constexpr char kCommandConfigTopicTemplate[] = "energy/nodes/%s/cmd/config";

WiFiClient g_mqtt_transport;
PubSubClient g_mqtt_client(g_mqtt_transport);

unsigned long g_last_mqtt_retry_ms = 0;
char g_pending_command_topic[kCommandTopicBufferSize] = {};
char g_pending_command_payload[kOutgoingPayloadMaxLength] = {};
bool g_has_pending_command = false;

void CopyString(char* destination, std::size_t destination_size, const char* source) {
    if (destination == nullptr || destination_size == 0) {
        return;
    }

    if (source == nullptr) {
        destination[0] = '\0';
        return;
    }

    std::snprintf(destination, destination_size, "%s", source);
}

void BuildCommandTopic(char* topic, std::size_t topic_size, const char* pattern) {
    if (topic == nullptr || topic_size == 0 || pattern == nullptr) {
        return;
    }

    std::snprintf(topic, topic_size, pattern, GetNodeId());
}

void SetMqttConnected(bool connected) {
    g_system_state.mqtt_connected = connected;
    UpdateSystemStatus();
}

void HandleMqttMessage(char* topic, std::uint8_t* payload, unsigned int length) {
    if (topic == nullptr) {
        return;
    }

    CopyString(g_pending_command_topic, sizeof(g_pending_command_topic), topic);

    const std::size_t safe_length =
        (length < (sizeof(g_pending_command_payload) - 1U)) ? length : (sizeof(g_pending_command_payload) - 1U);
    if (safe_length > 0) {
        std::memcpy(g_pending_command_payload, payload, safe_length);
    }
    g_pending_command_payload[safe_length] = '\0';
    g_has_pending_command = true;

    Serial.print("MQTT command received on topic: ");
    Serial.println(g_pending_command_topic);
}

void ConfigureMqttClient() {
    g_mqtt_client.setServer(g_runtime_config.mqtt_host, g_runtime_config.mqtt_port);
    g_mqtt_client.setCallback(HandleMqttMessage);
    g_mqtt_client.setKeepAlive(kMqttKeepAliveSec);
    g_mqtt_client.setBufferSize(kOutgoingPayloadMaxLength);
}

bool SubscribeCommandTopics() {
    char get_status_topic[kCommandTopicBufferSize] = {};
    char config_topic[kCommandTopicBufferSize] = {};

    BuildCommandTopic(get_status_topic, sizeof(get_status_topic), kCommandGetStatusTopicTemplate);
    BuildCommandTopic(config_topic, sizeof(config_topic), kCommandConfigTopicTemplate);

    return g_mqtt_client.subscribe(get_status_topic) && g_mqtt_client.subscribe(config_topic);
}

bool ConnectMqttClient() {
    if (!HasNodeIdentity() || !HasMqttBrokerConfig()) {
        return false;
    }

    char client_id[64] = {};
    std::snprintf(client_id, sizeof(client_id), "e1-node-%s", GetNodeId());

    bool connected = false;
    if (g_runtime_config.mqtt_username[0] != '\0') {
        connected = g_mqtt_client.connect(client_id,
                                          g_runtime_config.mqtt_username,
                                          g_runtime_config.mqtt_password);
    } else {
        connected = g_mqtt_client.connect(client_id);
    }

    if (!connected) {
        return false;
    }

    if (!SubscribeCommandTopics()) {
        g_mqtt_client.disconnect();
        return false;
    }

    SetMqttConnected(true);
    Serial.println("MQTT connected and subscribed");
    return true;
}

}  // namespace

void InitMqttManager() {
    ConfigureMqttClient();
    SetMqttConnected(false);
}

void RunMqttTask() {
    ConfigureMqttClient();

    if (!g_system_state.wifi_connected || !HasMqttBrokerConfig() || !HasNodeIdentity()) {
        if (g_mqtt_client.connected()) {
            g_mqtt_client.disconnect();
        }

        SetMqttConnected(false);
        return;
    }

    if (g_mqtt_client.connected()) {
        g_mqtt_client.loop();
        if (!g_mqtt_client.connected()) {
            SetMqttConnected(false);
        }
        return;
    }

    SetMqttConnected(false);

    const unsigned long now = millis();
    if (g_last_mqtt_retry_ms != 0 && (now - g_last_mqtt_retry_ms) < kMqttRetryIntervalMs) {
        return;
    }

    g_last_mqtt_retry_ms = now;
    ConnectMqttClient();
}

// Publishes a message to the given topic via the active MQTT connection.
// Returns true only when the broker accepted the message.
bool MqttPublish(const char* topic, const char* payload) {
    if (topic == nullptr || topic[0] == '\0' || payload == nullptr || payload[0] == '\0') {
        return false;
    }

    if (!g_system_state.wifi_connected || !g_mqtt_client.connected()) {
        SetMqttConnected(false);
        return false;
    }

    const bool published = g_mqtt_client.publish(topic, payload);
    if (!published && !g_mqtt_client.connected()) {
        SetMqttConnected(false);
    }

    return published;
}

bool ConsumePendingMqttCommand(char* topic,
                               std::size_t topic_size,
                               char* payload,
                               std::size_t payload_size) {
    if (!g_has_pending_command || topic == nullptr || payload == nullptr || topic_size == 0 || payload_size == 0) {
        return false;
    }

    CopyString(topic, topic_size, g_pending_command_topic);
    CopyString(payload, payload_size, g_pending_command_payload);

    g_pending_command_topic[0] = '\0';
    g_pending_command_payload[0] = '\0';
    g_has_pending_command = false;
    return true;
}
