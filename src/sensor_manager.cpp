#include "sensor_manager.h"

#include <Arduino.h>

#include <PZEM004Tv30.h>

#include "config.h"
#include "globals.h"

#include <cmath>
#include <ctime>

namespace {

constexpr unsigned long kSensorSampleIntervalMs = kSensorSampleIntervalSec * 1000UL;
constexpr std::uint8_t kSensorFailureThreshold = 3;

// Matches the PZEM library's documented ESP32 HardwareSerial example.
constexpr int kDefaultPzemRxPin = 16;
constexpr int kDefaultPzemTxPin = 17;

HardwareSerial g_pzem_serial(2);
PZEM004Tv30* g_pzem = nullptr;

unsigned long g_last_sensor_sample_ms = 0;
std::uint8_t g_consecutive_sensor_failures = 0;

std::uint32_t GetSampleTimestampSec() {
    const time_t now = time(nullptr);
    if (now > 0) {
        return static_cast<std::uint32_t>(now);
    }

    return millis() / 1000U;
}

bool IsValidMeasurement(float value) {
    return !std::isnan(value) && std::isfinite(value);
}

bool ReadPzemSample(SensorSample* sample) {
    if (sample == nullptr || g_pzem == nullptr) {
        return false;
    }

    const float voltage = g_pzem->voltage();
    const float current = g_pzem->current();
    const float power = g_pzem->power();
    const float energy_kwh = g_pzem->energy();
    const float frequency = g_pzem->frequency();
    const float power_factor = g_pzem->pf();

    if (!IsValidMeasurement(voltage) ||
        !IsValidMeasurement(current) ||
        !IsValidMeasurement(power) ||
        !IsValidMeasurement(energy_kwh) ||
        !IsValidMeasurement(frequency) ||
        !IsValidMeasurement(power_factor)) {
        return false;
    }

    sample->timestamp = GetSampleTimestampSec();
    sample->voltage = voltage;
    sample->current = current;
    sample->power = power;
    sample->energy_wh = energy_kwh * 1000.0F;
    sample->frequency_hz = frequency;
    sample->power_factor = power_factor;
    sample->valid = true;
    return true;
}

void UpdateSensorHealth(bool healthy) {
    g_system_state.sensor_ok = healthy;
    UpdateSystemStatus();
}

}  // namespace

void InitSensorManager() {
    g_last_sensor_sample_ms = 0;
    g_consecutive_sensor_failures = 0;

    if (g_pzem == nullptr) {
        g_pzem = new PZEM004Tv30(g_pzem_serial, kDefaultPzemRxPin, kDefaultPzemTxPin);
    }

    g_latest_sample = SensorSample{};
    UpdateSensorHealth(false);
}

void RunSensorTask() {
    const unsigned long now = millis();
    if (g_last_sensor_sample_ms != 0 && (now - g_last_sensor_sample_ms) < kSensorSampleIntervalMs) {
        return;
    }

    g_last_sensor_sample_ms = now;

    SensorSample sample = {};
    if (ReadPzemSample(&sample)) {
        g_latest_sample = sample;
        g_consecutive_sensor_failures = 0;
        UpdateSensorHealth(true);
        return;
    }

    if (g_consecutive_sensor_failures < kSensorFailureThreshold) {
        ++g_consecutive_sensor_failures;
    }

    if (g_consecutive_sensor_failures >= kSensorFailureThreshold) {
        UpdateSensorHealth(false);
    }
}
