#!/usr/bin/env bash

set -euo pipefail

base_url="${1:-http://127.0.0.1:8080}"
curl --fail --silent --show-error --max-time 5 "$base_url/health/live" >/dev/null
curl --fail --silent --show-error --max-time 5 "$base_url/health/ready" >/dev/null
echo "datastream health check passed: $base_url"
