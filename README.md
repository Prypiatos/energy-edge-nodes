# Energy Edge Nodes

This repository contains the firmware and shared interface specification for the E1 edge nodes in the smart energy monitoring system.
See [CONTRIBUTING.md](/home/sheron/Documents/energy-edge-nodes/CONTRIBUTING.md) for setup, commit conventions, and the issue and pull request workflow.

Each node is an ESP32-based device connected to a PZEM-004T energy meter. The node reads electrical measurements, detects local events, buffers unsent data, publishes MQTT messages, and responds to backend commands.

## Project Scope

The node firmware is expected to:

* connect to Wi-Fi and reconnect automatically when disconnected
* connect to MQTT and subscribe to command topics
* read PZEM data every second
* store readings in a queue
* publish telemetry every 2 seconds
* publish health every 30 seconds
* detect and publish events immediately
* buffer unsent outgoing messages
* flush buffered data after reconnection
* handle backend commands such as status requests and config updates
* maintain shared global system state safely

## Local Configuration

Runtime configuration is loaded from LittleFS at `/config.json`.

The repository tracks [data/config.example.json](/home/sheron/Documents/energy-edge-nodes/data/config.example.json) as a schema example and ignores `data/config.json` so each developer can keep node identity and Wi-Fi credentials out of git.

To provision a real ESP32:

1. Create `data/config.json` from the example file.
2. Fill in the real `node_id`, `node_type`, Wi-Fi credentials, and thresholds.
3. Validate the JSON locally.
4. Upload the LittleFS filesystem image.
5. Upload the firmware.

Validate the example config with:

```bash
./scripts/validate-config-example.sh
```

Example:

```bash
cp data/config.example.json data/config.json
./scripts/validate-config-example.sh data/config.json
pio run -t uploadfs
pio run -t upload
```

When the node boots, it reads `/config.json` from LittleFS.

<details>
<summary><strong>Internal Firmware Guide for the E1 Team</strong></summary>

## Internal Firmware Guide

This section is for the internal firmware team.

## Node Responsibilities

Each node has a unique `node_id` and one `node_type`.

Supported node types:

* `plug`
* `circuit`
* `main`

Each node publishes to MQTT using the shared external contract already agreed with the other teams.

---

## High-Level Runtime Model

The firmware should be split into independent tasks/modules.

Main logical parts:

* Wi-Fi manager
* MQTT manager
* sensor reader
* telemetry aggregator/publisher
* health publisher
* event detector/publisher
* command handler
* buffer manager
* shared state manager

The node should not rely on one giant loop with everything mixed together.

---

## Timing Requirements

### Sensor sampling

* every **1 second**

### Telemetry publish

* every **2 seconds**

### Health publish

* every **30 seconds**

### Events publish

* immediately when detected

---

## Recommended Folder Structure

```text
src/
  main.cpp
  config.h
  types.h
  globals.h
  globals.cpp
  wifi_manager.cpp
  wifi_manager.h
  mqtt_manager.cpp
  mqtt_manager.h
  sensor_manager.cpp
  sensor_manager.h
  telemetry_manager.cpp
  telemetry_manager.h
  event_manager.cpp
  event_manager.h
  health_manager.cpp
  health_manager.h
  command_manager.cpp
  command_manager.h
  buffer_manager.cpp
  buffer_manager.h
  time_manager.cpp
  time_manager.h
```

This does not need to be exact, but the code should be split by responsibility.

---

## Required Tasks

## Team Task Allocation

The implementation work for this repository is divided among the team members as follows.

| Member | Assigned work |
| --- | --- |
| Umaya | Wi-Fi task, reconnect handling, and connection state updates |
| Damindu | MQTT task, buffer manager task, and message retry/flush flow after reconnection |
| Yohan | Sensor read task, telemetry task, and health task |
| Kalhara | Event detection task, command handler task, and final integration/testing support |
| Sheron | Cross-cutting support across all modules, including pull request review, debugging, issue triage, integration support, and final stabilization/fixes |

### Ownership Notes

* Umaya keeps ownership of the Wi-Fi module as already assigned.
* Damindu handles broker communication and offline message recovery because those two parts are tightly coupled.
* Yohan handles the sensor-to-payload path, including reading meter data and publishing telemetry/health outputs.
* Kalhara handles event and command logic, then supports end-to-end integration and validation across the full node flow.
* Sheron supports the full repository across module boundaries by reviewing changes, resolving integration issues, debugging failures, and helping drive the final production-ready state.

## 1. Wi-Fi Task

### Purpose

Maintain Wi-Fi connectivity.

### Responsibilities

* connect to configured SSID/password at startup
* update connection state
* retry connection if disconnected
* avoid blocking the whole system during reconnect attempts

### Expected behavior

* on boot, try to connect
* if not connected, retry after a fixed delay
* if already connected, keep monitoring
* update global `wifi_connected`

### Suggested retry policy

* retry every 5 seconds
* optionally use exponential backoff later

### Inputs

* Wi-Fi credentials from config

### Outputs

* updates shared connection state
* signals MQTT task that networking is available

---

## 2. MQTT Task

### Purpose

Maintain MQTT connectivity and route incoming/outgoing MQTT traffic.

### Responsibilities

* connect to broker
* reconnect if disconnected
* subscribe to command topics after successful connection
* publish queued messages
* receive commands and pass them to command handler

### Subscribed topics

```text
energy/nodes/{node_id}/cmd/get_status
energy/nodes/{node_id}/cmd/config
```

### Published topics

```text
energy/nodes/{node_id}/telemetry
energy/nodes/{node_id}/events
energy/nodes/{node_id}/health
energy/nodes/{node_id}/status
```

### Expected behavior

* do not try to connect if Wi-Fi is down
* once connected, subscribe immediately
* set `mqtt_connected = true`
* if disconnected, set `mqtt_connected = false` and retry later

---

## 3. Sensor Read Task

### Purpose

Read PZEM-004T values every second.

### Responsibilities

* poll PZEM every 1 second
* validate data
* update latest reading
* push reading into sensor queue
* update sensor health state

### Data read from PZEM

* voltage
* current
* power
* energy
* frequency
* power factor

### Expected behavior

* if read succeeds, set `sensor_ok = true`
* if repeated reads fail, set `sensor_ok = false`
* store timestamp with every sample

### Notes

This task should only focus on reading and forwarding data. It should not do everything else.

---

## 4. Telemetry Task

### Purpose

Turn 1-second samples into one 2-second telemetry message.

### Responsibilities

* read samples from sensor queue
* maintain rolling collection of samples
* select or aggregate final values
* build telemetry payload
* publish via MQTT
* if publish fails, send to buffer manager

### Minimum required telemetry fields

* `node_id`
* `timestamp`
* `voltage`
* `current`
* `power`
* `energy_wh`

### Aggregation rule

At minimum, after 2 samples:

* use the latest valid reading for instantaneous values
* use current cumulative energy from the most recent valid reading

You may also later calculate averages internally, but only the agreed external contract should be published.

---

## 5. Event Detection Task

### Purpose

Detect local anomalies and publish events immediately.

### Supported events

* `power_spike`
* `overload_warning`
* `power_down`

### Responsibilities

* examine incoming sensor readings
* compare with previous readings and thresholds
* create event payload when a rule is triggered
* publish immediately
* if publish fails, buffer the event

### Event rules

#### Power spike

Trigger when power increases sharply between two consecutive readings.

Example logic:

```c
if ((current_power - previous_power) > power_spike_delta)
```

#### Overload warning

Trigger when current exceeds the configured threshold.

Example logic:

```c
if (current > current_warning_threshold)
```

#### Power down

Trigger when current or power drops to zero after previously being active.

Example logic:

```c
if (power == 0 && previous_power > 0)
```

### Notes

* avoid repeatedly publishing the exact same event every second
* use simple cooldown/debounce logic where needed

---

## 6. Health Task

### Purpose

Publish periodic node health.

### Responsibilities

* publish health every 30 seconds
* report connectivity, uptime, sensor health, and buffered count

### Required health fields

* `node_id`
* `node_type`
* `timestamp`
* `sequence_no`
* `status`
* `uptime_sec`
* `mqtt_connected`
* `wifi_connected`
* `sensor_ok`
* `buffered_count`

### Status values

* `online`
* `degraded`
* `offline_intended`

### Suggested health logic

* `online` when Wi-Fi, MQTT, and sensor are okay
* `degraded` when one part is unhealthy but the node is still running

### Sequence number rule

* increment for every health message

---

## 7. Command Handler Task

### Purpose

Process backend commands received through MQTT.

### Supported commands

* `get_status`
* `config`

### A. Get Status Command

#### Topic

```text
energy/nodes/{node_id}/cmd/get_status
```

#### Input example

```json
{
  "request_id": "req_00123",
  "requested_by": "backend"
}
```

#### Action

* read latest system state
* read latest sensor values
* build status response
* publish to:

```text
energy/nodes/{node_id}/status
```

#### Status payload should include

* `request_id`
* `node_id`
* `node_type`
* `timestamp`
* `status`
* `latest_voltage`
* `latest_current`
* `latest_power`
* `latest_energy_wh`
* `latest_frequency`
* `latest_power_factor`
* `sensor_ok`

---

### B. Config Command

#### Topic

```text
energy/nodes/{node_id}/cmd/config
```

#### Input example

```json
{
  "request_id": "cfg_0015",
  "publish_interval_sec": 2,
  "health_interval_sec": 30,
  "current_warning_threshold": 8.0,
  "current_critical_threshold": 10.0,
  "power_spike_delta": 300.0
}
```

#### Action

* validate input
* update runtime config
* store changes in global config/state
* optionally publish config confirmation event

### Important

Only supported config fields should be applied. Unknown fields should be ignored safely.

---

## 8. Buffer Manager Task

### Purpose

Prevent silent data loss when MQTT is unavailable.

### Responsibilities

* accept unsent telemetry/events/health messages
* store them in FIFO order
* retry sending when MQTT reconnects
* mark resent messages as `buffered: true`

### Buffer behavior

* if publish fails, enqueue message
* when MQTT becomes available, flush oldest first
* do not reorder messages
* track `buffered_count`

### Minimum implementation

* in-memory queue/ring buffer

### Better future implementation

* persistent storage using flash / LittleFS / NVS

### Important

Backend will use the embedded timestamp, so keep original timestamps when replaying buffered messages.

---

## Shared Data Structures

Below is a suggested internal structure. It does not need to be exactly this, but the same concepts should exist.

## Sensor sample structure

```c
typedef struct {
    uint32_t timestamp;
    float voltage;
    float current;
    float power;
    float energy_wh;
    float frequency;
    float power_factor;
    bool valid;
} SensorSample;
```

---

## Event structure

```c
typedef struct {
    char event_type[32];
    char severity[16];
    char message[128];
    uint32_t timestamp;
    bool buffered;
} EventMessage;
```

---

## Outgoing MQTT message structure

```c
typedef struct {
    char topic[128];
    char payload[512];
    bool buffered;
} OutgoingMessage;
```

---

## Runtime configuration structure

```c
typedef struct {
    uint32_t telemetry_interval_sec;
    uint32_t health_interval_sec;
    float current_warning_threshold;
    float current_critical_threshold;
    float power_spike_delta;
} RuntimeConfig;
```

---

## Global system state structure

```c
typedef struct {
    bool wifi_connected;
    bool mqtt_connected;
    bool sensor_ok;
    uint32_t uptime_sec;
    uint32_t telemetry_sequence_no;
    uint32_t health_sequence_no;
    uint32_t buffered_count;
    char status[24];
} SystemState;
```

You may use one shared sequence counter instead of separate ones, but keep it consistent.

---

## Global Variables

Global shared variables are allowed, but they should be controlled carefully.

### Recommended globals

* current system state
* runtime config
* latest valid sensor sample
* queue handles
* MQTT client handle
* node metadata such as `node_id` and `node_type`

### Example

```c
extern SystemState g_system_state;
extern RuntimeConfig g_runtime_config;
extern SensorSample g_latest_sample;
extern QueueHandle_t g_sensor_queue;
extern QueueHandle_t g_buffer_queue;
```

### Rules

* do not let every file write to everything carelessly
* protect shared data if multiple tasks access it
* if using FreeRTOS, use mutexes where needed
* prefer queues for task-to-task communication
* use globals mainly for shared current state, not for passing large message flows

---

## Queues

At minimum, the firmware should have these queues.

## 1. Sensor queue

Purpose:

* send fresh samples from sensor task to telemetry/event logic

Type:

* `SensorSample`

---

## 2. Buffer queue

Purpose:

* hold unsent outgoing MQTT messages

Type:

* `OutgoingMessage`

---

## Optional queues

You may later add:

* command queue
* event queue
* publish queue

But the minimum two above are enough to start.

---

## State Management Rules

### Wi-Fi state

* `true` only when connected and IP acquired

### MQTT state

* `true` only when broker connection is active and usable

### Sensor state

* `true` when recent reads are successful
* `false` after repeated failures

### Overall node status

Set from combined subsystem health:

* `online`
* `degraded`
* `offline_intended`

---

## Message Publishing Rules

## Telemetry

* publish every 2 seconds
* QoS 1
* increment telemetry sequence number
* if failed, buffer

## Events

* publish immediately
* QoS 1
* no sequence number in external event contract
* if failed, buffer

## Health

* publish every 30 seconds
* QoS 0 or 1
* increment health sequence number
* if failed, may buffer depending on implementation choice

## Status

* publish only when `get_status` command is received
* QoS 1

---

## Wi-Fi Reconnect Logic

Suggested logic:

1. start Wi-Fi
2. attempt connection
3. if connected:
   * set `wifi_connected = true`
4. if disconnected:
   * set `wifi_connected = false`
   * wait retry interval
   * retry

### Important

The Wi-Fi task must keep running for the lifetime of the node.

---

## MQTT Reconnect Logic

Suggested logic:

1. if Wi-Fi is not connected, do not attempt MQTT
2. if Wi-Fi is connected, attempt MQTT connection
3. on success:
   * set `mqtt_connected = true`
   * subscribe to command topics
4. on failure:
   * set `mqtt_connected = false`
   * retry after delay

---

## Buffer Flush Logic

When MQTT reconnects:

1. check if the buffer is not empty
2. pop the oldest message
3. mark payload `buffered = true`
4. publish
5. if publish succeeds, remove it permanently
6. continue until the queue is empty or the connection fails again

This must be FIFO.

---

## Time Handling

The node should timestamp all outgoing messages.

### Recommended approach

* use NTP after Wi-Fi connection
* store UTC timestamps
* use ISO 8601 format in payloads

If real UTC is not available at first boot, you may temporarily use uptime-based timestamps internally, but published messages should eventually use proper UTC.

---

## Minimum Implementation Checklist

Before calling the node "working," it must support all of the following:

* Wi-Fi connection on boot
* Wi-Fi reconnect after disconnect
* MQTT connection on boot
* MQTT reconnect after disconnect
* PZEM reading every second
* valid latest sample storage
* telemetry publish every 2 seconds
* health publish every 30 seconds
* immediate event detection and publish
* get status command handling
* config command handling
* buffer queue for failed publishes
* buffer flush after reconnect
* correct MQTT topics
* correct payload fields
* safe shared global state

---

## Suggested Development Order

Build in this order:

### Phase 1

* project setup
* config file
* Wi-Fi connection
* serial logging

### Phase 2

* PZEM integration
* read voltage/current/power every second
* print readings locally

### Phase 3

* MQTT connection
* publish test messages
* subscribe to command topics

### Phase 4

* sensor queue
* latest sample state
* telemetry payload builder

### Phase 5

* publish telemetry every 2 seconds
* publish health every 30 seconds

### Phase 6

* event detection
* event publishing

### Phase 7

* buffer queue
* resend logic after reconnect

### Phase 8

* status command handling
* config command handling

### Phase 9

* cleanup, testing, fault simulation, documentation

---

## Testing Checklist

## Connectivity

* node connects to Wi-Fi on boot
* node reconnects after router restart
* node reconnects to MQTT after broker restart

## Sensor

* readings are updated every second
* invalid reads are handled safely
* `sensor_ok` changes correctly on failures

## Telemetry

* telemetry is published once per 2 seconds
* payload matches agreed schema
* sequence number increments correctly

## Events

* `power_spike` triggers when expected
* `overload_warning` triggers when expected
* `power_down` triggers when expected
* event payload matches agreed schema

## Health

* health is published every 30 seconds
* connectivity fields are correct

## Commands

* `get_status` returns correct latest values
* `config` updates runtime settings correctly

## Buffering

* failed publish adds message to buffer
* reconnection flushes buffer in order
* replayed messages are marked `buffered: true`

---

## Coding Rules

* keep each task focused on one responsibility
* do not hardcode logic everywhere
* define all topics and constants in one place
* keep payload generation centralized
* use clear names
* log important failures
* do not silently ignore errors
* protect shared state access
* avoid blocking delays inside critical tasks

---

## Future Improvements

These are not required for the first version, but can be added later:

* persistent buffering in flash
* OTA firmware updates
* secure MQTT with authentication/TLS
* per-event cooldown logic
* watchdog recovery
* remote reboot command
* advanced event rules
* more detailed diagnostics

---

## Final Goal

A working E1 node should behave like this:

1. boots
2. connects to Wi-Fi
3. connects to MQTT
4. reads PZEM every second
5. stores samples
6. publishes telemetry every 2 seconds
7. publishes health every 30 seconds
8. publishes events immediately when detected
9. handles backend commands
10. buffers data during outages
11. flushes buffered data after reconnection

That is the minimum complete firmware behavior for this team.

</details>

<details>
<summary><strong>Interface Specification for External Contributors</strong></summary>

## Purpose

This document defines the MQTT topics, message structures, and example payloads that the other teams can rely on while building the backend, analytics, and dashboard layers.

This spec covers only the shared interface between E1 edge nodes and the rest of the system.

---

## System Overview

Each energy-monitoring node has a unique `node_id` and publishes messages over MQTT.

The main message categories are:

* `telemetry` for periodic sensor summaries
* `events` for anomalies and important state changes
* `health` for node heartbeat and health monitoring
* `status` for direct replies to status requests

Nodes also subscribe to command topics from the backend.

---

## Topic Naming Convention

All topics are rooted under:

```text
energy/nodes/{node_id}/
```

Topic patterns are:

```text
energy/nodes/{node_id}/{message_type}
energy/nodes/{node_id}/cmd/{command}
```

Examples:

```text
energy/nodes/plug_01/telemetry
energy/nodes/plug_01/events
energy/nodes/plug_01/health
energy/nodes/plug_01/status
energy/nodes/plug_01/cmd/get_status
energy/nodes/plug_01/cmd/config
```

Where:

* `message_type` is one of `telemetry`, `events`, `health`, or `status`
* `command` is one of `get_status` or `config`

---

## Node Types

Supported node types:

* `plug`
* `circuit`
* `main`

---

## Shared Identification Fields

The following fields should appear in most published messages:

| Field         | Type    | Description                                                       |
| ------------- | ------- | ----------------------------------------------------------------- |
| `node_id`     | string  | Unique node identifier                                            |
| `node_type`   | string  | `plug`, `circuit`, or `main`                                      |
| `timestamp`   | string  | ISO 8601 UTC timestamp                                            |
| `sequence_no` | integer | Monotonic message number from node                                |
| `buffered`    | boolean | Whether the message was sent from local buffer after reconnection |

---

## 1. Telemetry Topic

### Topic

```text
energy/nodes/{node_id}/telemetry
```

### Purpose

Periodic energy readings and summary values used by the backend, analytics, and dashboard teams.

### Publish Frequency

Collected every second and published every 2 seconds.

### Required Fields

| Field          | Type     | Description                           |
| -------------- | -------  | ------------------------------------- |
| `node_id`      | string   | Unique node identifier                |
| `timestamp`    | integer  | Message creation time in epoch ms     |
| `voltage`      | number   | Voltage in volts                      |
| `current`      | number   | Current in amps                       |
| `power`        | number   | Active power in watts                 |
| `energy_wh`    | number   | Cumulative energy in watt-hours       |

### Example Payload

```json
{
  "node_id": "plug_01",
  "timestamp": 1618032900000,
  "voltage": 230.1,
  "current": 1.78,
  "power": 401.6,
  "energy_wh": 1250.4,
}
```

---

## 2. Events Topic

### Topic

```text
energy/nodes/{node_id}/events
```

### Purpose

Immediate notifications for anomalies and important state changes.

### Publish Trigger

Published only when an important event occurs.

### Required Fields

| Field        | Type    | Description                              |
| ------------ | ------- | ---------------------------------------- |
| `node_id`    | string  | Unique node identifier                   |
| `node_type`  | string  | `plug`, `circuit`, or `main`             |
| `timestamp`  | string  | Event time                               |
| `event_type` | string  | Type of event                            |
| `severity`   | string  | `low`, `medium`, `high`, `critical`      |
| `message`    | string  | Short human-readable description         |
| `buffered`   | boolean | Whether the event was delayed and sent later |

### Supported Event Types

* `power_spike`
* `overload_warning`
* `power_down`

### Example Payload

```json
{
  "node_id": "circuit_01",
  "node_type": "circuit",
  "timestamp": "2026-04-10T10:16:22Z",
  "event_type": "overload_warning",
  "severity": "high",
  "message": "Current exceeded warning threshold",
  "buffered": false
}
```

---

## 3. Health Topic

### Topic

```text
energy/nodes/{node_id}/health
```

### Purpose

Heartbeat and device health monitoring for backend monitoring and dashboard status display.

### Publish Frequency

Recommended every 30 seconds.

### Required Fields

| Field            | Type    | Description                                 |
| ---------------- | ------- | ------------------------------------------- |
| `node_id`        | string  | Unique node identifier                      |
| `node_type`      | string  | `plug`, `circuit`, or `main`                |
| `timestamp`      | string  | Heartbeat time                              |
| `sequence_no`    | integer | Monotonic message number from node          |
| `status`         | string  | `online`, `degraded`, or `offline_intended` |
| `uptime_sec`     | integer | Time since boot                             |
| `mqtt_connected` | boolean | MQTT connection status                      |
| `wifi_connected` | boolean | Wi-Fi connection status                     |
| `sensor_ok`      | boolean | Sensor read health                          |
| `buffered_count` | integer | Number of unsent buffered messages          |

### Example Payload

```json
{
  "node_id": "main_01",
  "node_type": "main",
  "timestamp": "2026-04-10T10:16:30Z",
  "sequence_no": 220,
  "status": "online",
  "uptime_sec": 1840,
  "mqtt_connected": true,
  "wifi_connected": true,
  "sensor_ok": true,
  "buffered_count": 3
}
```

---

## 4. Status Topic

### Topic

```text
energy/nodes/{node_id}/status
```

### Purpose

Used as a direct response topic when backend services request live node status.

### Publish Trigger

Published only in response to a command on `cmd/get_status`.

### Required Fields

| Field            | Type    | Description                        |
| ---------------- | ------- | ---------------------------------- |
| `request_id`     | string  | Correlates response to request     |
| `node_id`        | string  | Unique node identifier             |
| `node_type`      | string  | `plug`, `circuit`, or `main`       |
| `timestamp`      | string  | Response time                      |
| `status`         | string  | Current node state                 |
| `latest_voltage` | number  | Latest voltage reading             |
| `latest_current` | number  | Latest current reading             |
| `latest_power`   | number  | Latest power reading               |
| `latest_energy_wh`    | number  | Latest cumulative energy (Wh) |
| `latest_frequency`    | number  | Latest grid frequency (Hz)    |
| `latest_power_factor` | number  | Latest power factor           |
| `sensor_ok`           | boolean | Sensor state                  |

### Example Payload

```json
{
  "request_id": "req_00123",
  "node_id": "plug_01",
  "node_type": "plug",
  "timestamp": "2026-04-10T10:17:00Z",
  "status": "online",
  "latest_voltage": 229.9,
  "latest_current": 1.83,
  "latest_power": 406.2,
  "latest_energy_wh": 1251.0,
  "latest_frequency": 50.0,
  "latest_power_factor": 0.95,
  "sensor_ok": true
}
```

---

## 5. Command Topics

Nodes subscribe to command topics from backend services.

### 5.1 Get Status Command

#### Topic

```text
energy/nodes/{node_id}/cmd/get_status
```

#### Purpose

Request a live node status response.

#### Example Payload

```json
{
  "request_id": "req_00123",
  "requested_by": "backend"
}
```

#### Expected Response

Node publishes to:

```text
energy/nodes/{node_id}/status
```

---

### 5.2 Config Command

#### Topic

```text
energy/nodes/{node_id}/cmd/config
```

#### Purpose

Send configuration updates such as publish interval and thresholds.

#### Example Payload

```json
{
  "request_id": "cfg_0015",
  "publish_interval_sec": 2,
  "health_interval_sec": 30,
  "current_warning_threshold": 8.0,
  "current_critical_threshold": 10.0,
  "power_spike_delta": 300.0
}
```

#### Suggested Acknowledgement

Node can publish a config-applied event to:

```text
energy/nodes/{node_id}/events
```

Example:

```json
{
  "node_id": "circuit_01",
  "node_type": "circuit",
  "timestamp": "2026-04-10T10:18:00Z",
  "event_type": "config_applied",
  "severity": "low",
  "message": "Configuration updated successfully",
  "buffered": false
}
```

---

## QoS Recommendation

Recommended MQTT QoS levels:

| Topic Type  | Recommended QoS |
| ----------- | --------------- |
| `telemetry` | 1               |
| `events`    | 1               |
| `health`    | 0 or 1          |
| `status`    | 1               |
| `cmd/*`     | 1               |

Reasoning:

* telemetry should not be silently lost
* events are important and should be delivered at least once
* health messages can use a lower QoS if traffic must be reduced

---


## Buffering Rule for Other Teams

Other teams should expect that some incoming telemetry and event messages may have:

```json
"buffered": true
```

This means the data was collected earlier during a connection failure and published later after reconnection.

Backend and analytics components should therefore:

* use the embedded `timestamp` as the actual event time
* not assume arrival time equals measurement time
* store and process delayed messages correctly

---

## Node Liveness Rule for Other Teams

A node should be considered:

* **online** if a health message has been received recently
* **possibly offline** if no health message is received within the agreed timeout window

Suggested timeout:

* health interval: 30 seconds
* offline timeout: 90 seconds

---

## Example Topic Summary

```text
Publish:
energy/nodes/{node_id}/telemetry
energy/nodes/{node_id}/events
energy/nodes/{node_id}/health
energy/nodes/{node_id}/status

Subscribe:
energy/nodes/{node_id}/cmd/get_status
energy/nodes/{node_id}/cmd/config
```

---

## Final Notes for Other Teams

* All node communication is MQTT-based.
* Arrival order may not always match event time because buffered data can be replayed.
* Health and status messages should be used for node availability views.
* Event messages are designed for alerting and real-time notification logic.
* Telemetry messages are designed for time-series storage, analytics, and dashboards.

</details>
