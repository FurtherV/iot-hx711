#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

avahi-publish-service "Dev MQTT" _mqtt._tcp 1883 &
AVAHI_PID=$!

cleanup() {
  kill "$AVAHI_PID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

mosquitto -c "$SCRIPT_DIR/mosquitto-dev.conf" -v
