#!/usr/bin/env bash

set -euo pipefail

for pattern in '/logtrace_gateway ' '/logsearch_server '; do
    mapfile -t pids < <(pgrep -f "$pattern" || true)
    if [[ ${#pids[@]} -gt 0 ]]; then
        kill "${pids[@]}"
    fi
done
