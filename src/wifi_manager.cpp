#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "globals.h"
#include "wifi_manager.h"

static void TaskWifi(void* pvParameters) {
    (void)pvParameters;
    vTaskDelay(pdMS_TO_TICKS(kWifiShortDelayMs));

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);

    while (true)
    {
        if (!HasWifiCredentials()) {
            g_system_state.wifi_connected = false;
            UpdateSystemStatus();
            vTaskDelay(pdMS_TO_TICKS(kWifiConnectedCheckMs));
            continue;
        }

        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.print("Connecting to WiFi: ");
            Serial.println(g_runtime_config.wifi_ssid);

            g_system_state.wifi_connected = false;
            UpdateSystemStatus();

            WiFi.disconnect(true);
            vTaskDelay(pdMS_TO_TICKS(kWifiShortDelayMs));

            WiFi.begin(g_runtime_config.wifi_ssid, g_runtime_config.wifi_password);

            int tries = 0;

            while (WiFi.status() != WL_CONNECTED && tries++ < kWifiMaxRetryCount)
            {
                vTaskDelay(pdMS_TO_TICKS(kWifiShortDelayMs));
                Serial.print(".");
            }

            Serial.println();

            if (WiFi.status() == WL_CONNECTED)
            {
                Serial.print("WiFi connected. IP: ");
                Serial.println(WiFi.localIP());

                Serial.print("RSSI: ");
                Serial.println(WiFi.RSSI());

                g_system_state.wifi_connected = true;
                UpdateSystemStatus();
            }
            else
            {
                Serial.println("WiFi connect failed, retrying...");
                g_system_state.wifi_connected = false;
                UpdateSystemStatus();

                vTaskDelay(pdMS_TO_TICKS(kWifiRetryBackoffMs));
            }
        }
        else
        {
            g_system_state.wifi_connected = true;
            UpdateSystemStatus();
            vTaskDelay(pdMS_TO_TICKS(kWifiConnectedCheckMs));
        }
    }
}

void InitWifiManager() {
    Serial.println("WiFi Manager Initialized");
}

void RunWifiTask() {
    BaseType_t result = xTaskCreatePinnedToCore(
    TaskWifi,
    "TaskWifi",
    8192,
    nullptr,
    1,
    nullptr,
    0
    );

    if (result != pdPASS) {
        Serial.println("Failed to create WiFi task");
    }
}
