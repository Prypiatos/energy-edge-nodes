#include "wifi_manager.h"
#include <WiFi.h>
#include "globals.h"
#include "wifi_config.h"

void TaskWifi(void* pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(200));

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);

    while (true)
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.print("Connecting to WiFi: ");
            Serial.println(WIFI_SSID);

            g_system_state.wifi_connected = false;

            WiFi.disconnect(true);
            vTaskDelay(pdMS_TO_TICKS(200));

            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

            int tries = 0;

            while (WiFi.status() != WL_CONNECTED && tries++ < 50)
            {
                vTaskDelay(pdMS_TO_TICKS(200));
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
            }
            else
            {
                Serial.println("WiFi connect failed, retrying...");
                g_system_state.wifi_connected = false;

                vTaskDelay(pdMS_TO_TICKS(2000));
            }
        }
        else
        {
            g_system_state.wifi_connected = true;
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }
}

void InitWifiManager() {
    Serial.println("WiFi Manager Initialized");
}

void RunWifiTask() {
    xTaskCreatePinnedToCore(
        TaskWifi,
        "TaskWifi",
        8192,
        nullptr,
        1,
        nullptr,
        0
    );
}