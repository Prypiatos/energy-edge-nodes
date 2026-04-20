#include "mqtt_manager.h"
#include "globals.h"
#include "config.h"

#include <PubSubClient.h>
#include <WiFi.h>
#include <cstdio>
#include <cstring>

// ─────────────────────────────────────────
// Broker settings — update kBrokerHost before flashing
// ─────────────────────────────────────────
static constexpr char kBrokerHost[] = "192.168.1.100"; // TODO: update to actual broker IP
static constexpr int  kBrokerPort   = 1883;
static constexpr int  kMqttRetryIntervalMs = 5000;

// ─────────────────────────────────────────
// Internal state
// ─────────────────────────────────────────
static WiFiClient   s_wifi_client;
static PubSubClient s_mqtt_client(s_wifi_client);

// ─────────────────────────────────────────
// Forward declarations
// ─────────────────────────────────────────
static void OnMessageReceived(const char* topic, byte* payload, unsigned int length);
static bool TryConnect();
static void SubscribeToCommandTopics();
static void BuildCommandTopic(char* buffer, std::size_t size, const char* command);

// ─────────────────────────────────────────
// Public — called once at startup
// ─────────────────────────────────────────
void InitMqttManager() {
    s_mqtt_client.setServer(kBrokerHost, kBrokerPort);
    s_mqtt_client.setCallback(OnMessageReceived);
}

// ─────────────────────────────────────────
// Public — call this in a loop / task
// ─────────────────────────────────────────
void RunMqttTask() {
    while (true) {

        // Do not attempt MQTT if Wi-Fi is down
        if (!g_system_state.wifi_connected) {
            g_system_state.mqtt_connected = false;
            delay(kMqttRetryIntervalMs);
            continue;
        }

        // If disconnected, attempt to reconnect
        if (!s_mqtt_client.connected()) {
            g_system_state.mqtt_connected = false;
            if (!TryConnect()) {
                delay(kMqttRetryIntervalMs);
                continue;
            }
        }

        // Keep connection alive and receive incoming messages
        s_mqtt_client.loop();
        delay(100);
    }
}

// ─────────────────────────────────────────
// Publish — used by telemetry, health, etc.
// ─────────────────────────────────────────
bool MqttPublish(const char* topic, const char* payload) {
    if (!s_mqtt_client.connected()) {
        return false;
    }
    return s_mqtt_client.publish(topic, payload);
}

// ─────────────────────────────────────────
// Internal — connect to broker
// ─────────────────────────────────────────
static bool TryConnect() {
    std::printf("[MQTT] Attempting connection to %s:%d...\n", kBrokerHost, kBrokerPort);

    const bool connected = s_mqtt_client.connect(kDefaultNodeId);

    if (connected) {
        g_system_state.mqtt_connected = true;
        std::printf("[MQTT] Connected as %s\n", kDefaultNodeId);
        SubscribeToCommandTopics();
    } else {
        g_system_state.mqtt_connected = false;
        std::printf("[MQTT] Connection failed, state=%d\n", s_mqtt_client.state());
    }

    return connected;
}

// ─────────────────────────────────────────
// Internal — subscribe to command topics
// ─────────────────────────────────────────
static void SubscribeToCommandTopics() {
    char topic[128];

    BuildCommandTopic(topic, sizeof(topic), "get_status");
    s_mqtt_client.subscribe(topic);
    std::printf("[MQTT] Subscribed to %s\n", topic);

    BuildCommandTopic(topic, sizeof(topic), "config");
    s_mqtt_client.subscribe(topic);
    std::printf("[MQTT] Subscribed to %s\n", topic);
}

// ─────────────────────────────────────────
// Internal — build topic string
// ─────────────────────────────────────────
static void BuildCommandTopic(char* buffer, std::size_t size, const char* command) {
    std::snprintf(buffer, size, "energy/nodes/%s/cmd/%s", kDefaultNodeId, command);
}

// ─────────────────────────────────────────
// Internal — handle incoming commands
// ─────────────────────────────────────────
static void OnMessageReceived(const char* topic, byte* payload, unsigned int length) {
    // Safety: null-terminate the payload
    char message[512] = {};
    const unsigned int safe_length = length < sizeof(message) - 1 ? length : sizeof(message) - 1;
    std::memcpy(message, payload, safe_length);
    message[safe_length] = '\0';

    std::printf("[MQTT] Message received on %s: %s\n", topic, message);

    // Build expected command topics for comparison
    char get_status_topic[128];
    char config_topic[128];
    BuildCommandTopic(get_status_topic, sizeof(get_status_topic), "get_status");
    BuildCommandTopic(config_topic,     sizeof(config_topic),     "config");

    if (std::strcmp(topic, get_status_topic) == 0) {
        std::printf("[MQTT] get_status command received — handler not yet implemented\n");
        // command_manager will handle this in a later sprint
    } else if (std::strcmp(topic, config_topic) == 0) {
        std::printf("[MQTT] config command received — handler not yet implemented\n");
        // command_manager will handle this in a later sprint
    } else {
        std::printf("[MQTT] Unknown topic: %s\n", topic);
    }
}