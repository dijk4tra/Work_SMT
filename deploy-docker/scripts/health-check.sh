#!/usr/bin/env bash

set -euo pipefail

datastream_url="${SMT_DATASTREAM_URL:-http://127.0.0.1:${SMT_DATASTREAM_PORT:-9090}}"
logtrace_url="${SMT_LOGTRACE_URL:-http://127.0.0.1:${SMT_LOGTRACE_PORT:-9091}}"

wait_for_endpoint() {
    local url="$1"
    local deadline=$((SECONDS + 60))
    while ! curl --fail --silent --show-error --max-time 5 "$url" >/dev/null; do
        if (( SECONDS >= deadline )); then
            echo "health endpoint did not become ready: $url" >&2
            return 1
        fi
        sleep 2
    done
}

wait_for_endpoint "$datastream_url/health/live"
wait_for_endpoint "$datastream_url/health/ready"
wait_for_endpoint "$logtrace_url/health/live"
wait_for_endpoint "$logtrace_url/health/ready"
echo "DataStream and LogTrace are ready"
