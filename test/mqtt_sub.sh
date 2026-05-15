#!/usr/bin/env bash
set -euo pipefail

HOST="${1:-127.0.0.1}"
TOPIC="${2:-#}"

mosquitto_sub -h "$HOST" -t "$TOPIC" -v
