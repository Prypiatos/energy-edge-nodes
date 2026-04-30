#!/usr/bin/env sh

set -eu

repo_root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
config_path="${1:-"$repo_root/data/config.example.json"}"

jq -e '
  .node_id | type == "string" and length > 0
' "$config_path" >/dev/null

jq -e '
  .node_type | IN("plug", "circuit", "main")
' "$config_path" >/dev/null

jq -e '
  .wifi_ssid | type == "string"
' "$config_path" >/dev/null

jq -e '
  .wifi_password | type == "string"
' "$config_path" >/dev/null

jq -e '
  (.telemetry_interval_sec | type == "number" and . > 0) and
  (.health_interval_sec | type == "number" and . > 0) and
  (.current_warning_threshold | type == "number" and . > 0) and
  (.current_critical_threshold | type == "number" and . > 0) and
  (.power_spike_delta | type == "number" and . > 0)
' "$config_path" >/dev/null

printf '%s\n' "Config JSON looks valid: $config_path"
