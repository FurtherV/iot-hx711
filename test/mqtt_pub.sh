#!/usr/bin/env bash
set -euo pipefail

HOST="${1:-127.0.0.1}"
TOPIC="${2:-demo/data}"
PAYLOAD="{\"temperature\":$((20 + RANDOM % 10)),\"humidity\":$((40 + RANDOM % 30)),\"status\":\"online\",\"timestamp\":\"$(date --iso-8601=seconds)\"}"

mosquitto_pub -h "$HOST" -t "$TOPIC" -m "$PAYLOAD"
