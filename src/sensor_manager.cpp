// sensor_manager.cpp
// Owner: Yohan
// Responsibility:
//   - Poll PZEM-004T every 1 second
//   - Validate the reading (reject NaN values)
//   - Update g_latest_sample and g_system_state.sensor_ok in globals
//   - Called from loop() in main.cpp via RunSensorTask()

#include "sensor_manager.h"

#include "config.h"
#include "globals.h"

#include <Arduino.h>
#include <PZEM004Tv30.h>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// PZEM-004T connected to ESP32 Serial2
// RX2 = GPIO16, TX2 = GPIO17 (standard ESP32 Serial2 pins)
// ─────────────────────────────────────────────────────────────────────────────
static PZEM004Tv30 g_pzem(Serial2, 16, 17);

// Number of consecutive failed reads before sensor is marked unhealthy
static constexpr int kFailureThreshold = 3;
static int g_consecutive_failures = 0;

// Tracks when we last took a sample (milliseconds)
static unsigned long g_last_sample_ms = 0;

// ─────────────────────────────────────────────────────────────────────────────
// InitSensorManager
// Called once from setup() in main.cpp
// ─────────────────────────────────────────────────────────────────────────────
void InitSensorManager() {

    Serial2.begin(9600, SERIAL_8N1, 16, 17); 
    
    g_consecutive_failures = 0;
    g_last_sample_ms = 0;
    Serial.println("[sensor] Sensor manager initialised");
}

// ─────────────────────────────────────────────────────────────────────────────
// TryReadPzem (private helper)
// Reads all fields from PZEM. Returns false if any field is NaN.
// Only fills fields defined in SensorSample (types.h):
//   timestamp, voltage, current, power, energy_wh, valid
// frequency and power_factor are read for validation but not stored
// because they are not in the current struct definition.
// ─────────────────────────────────────────────────────────────────────────────
static bool TryReadPzem(SensorSample& sample) {
    const float voltage      = g_pzem.voltage();
    const float current      = g_pzem.current();
    const float power        = g_pzem.power();
    const float energy_kwh   = g_pzem.energy();   // PZEM gives kWh
    const float frequency    = g_pzem.frequency();
    const float power_factor = g_pzem.pf();

    // PZEM returns NaN for every field when the read fails
    if (std::isnan(voltage)      || std::isnan(current)      ||
        std::isnan(power)        || std::isnan(energy_kwh)   ||
        std::isnan(frequency)    || std::isnan(power_factor)) {
        return false;
    }

    sample.timestamp = static_cast<std::uint32_t>(millis() / 1000UL);
    sample.voltage   = voltage;
    sample.current   = current;
    sample.power     = power;
    sample.energy_wh = energy_kwh * 1000.0f;   // kWh → Wh
    sample.valid     = true;

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// RunSensorTask
// Called every loop() iteration from main.cpp.
// Uses elapsed-time gating to sample exactly once per kSensorSampleIntervalSec.
// Updates g_latest_sample and g_system_state.sensor_ok in globals.h
// ─────────────────────────────────────────────────────────────────────────────
void RunSensorTask() {
    const unsigned long now_ms = millis();

    // Only sample when the interval has elapsed
    if (now_ms - g_last_sample_ms < (kSensorSampleIntervalSec * 1000UL)) {
        return;
    }
    g_last_sample_ms = now_ms;

    SensorSample sample = {};
    const bool ok = TryReadPzem(sample);

    if (ok) {
        // ── Successful read ──────────────────────────────────────────────
        g_consecutive_failures   = 0;
        g_latest_sample          = sample;       // shared global for event/telemetry
        g_system_state.sensor_ok = true;

        Serial.printf("[sensor] V=%.1fV I=%.2fA P=%.1fW E=%.1fWh\n",
                      sample.voltage, sample.current,
                      sample.power,   sample.energy_wh);

    } else {
        // ── Failed read ──────────────────────────────────────────────────
        g_consecutive_failures++;
        Serial.printf("[sensor] Read failed (%d/%d)\n",
                      g_consecutive_failures, kFailureThreshold);

        if (g_consecutive_failures >= kFailureThreshold) {
            g_system_state.sensor_ok = false;
            Serial.println("[sensor] Sensor marked UNHEALTHY");
        }
    }
}
