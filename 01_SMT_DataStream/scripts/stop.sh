#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: scripts/stop.sh <pid-file>" >&2
    exit 2
fi

pid_file="$1"
pid="$(<"$pid_file")"
if [[ ! "$pid" =~ ^[1-9][0-9]*$ ]]; then
    echo "invalid pid file: $pid_file" >&2
    exit 1
fi
kill -TERM "$pid"
while kill -0 "$pid" 2>/dev/null; do sleep 0.1; done
rm -f "$pid_file"
