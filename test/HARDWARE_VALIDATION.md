# Hardware Validation

Use this checklist when validating a real node against a live MQTT broker.

## Connectivity

- Boot the node with valid Wi-Fi and broker settings and confirm Wi-Fi + MQTT connect successfully.
- Restart the router or temporarily remove Wi-Fi coverage and confirm the node reconnects automatically.
- Restart the MQTT broker and confirm the node reconnects automatically.

## Sensor

- Confirm PZEM readings update every second.
- Confirm invalid or disconnected sensor reads eventually drive `sensor_ok` to `false`.
- Confirm valid reads restore `sensor_ok` to `true`.

## Telemetry and Health

- Confirm telemetry publishes on the configured interval.
- Confirm health publishes on the configured interval.
- Confirm telemetry and health payloads use the expected node identity and timestamps.
- Confirm telemetry and health sequence numbers increase monotonically.

## Events

- Trigger a power spike condition and confirm a `power_spike` event is emitted once per cooldown window.
- Trigger an overload condition and confirm `overload_warning` fires on the rising edge.
- Trigger an active-to-zero transition and confirm `power_down` is emitted.

## Buffering and Recovery

- Disconnect the broker while the node is running and confirm telemetry, health, and events buffer instead of disappearing silently.
- Reconnect the broker and confirm buffered messages flush in FIFO order.
- Confirm buffered payloads keep `buffered = true` after replay.

## Commands

- Publish `cmd/get_status` and confirm the node replies on the `status` topic with the latest sample fields.
- Publish `cmd/config` with supported interval/threshold fields and confirm the updated config persists across reboot.
