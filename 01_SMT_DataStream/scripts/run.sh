#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: scripts/run.sh <server|collector> <config-path>" >&2
    exit 2
fi

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
case "$1" in
    server) binary="$project_root/build/datastream_server" ;;
    collector) binary="$project_root/build/collector_agent" ;;
    *) echo "component must be server or collector" >&2; exit 2 ;;
esac

exec "$binary" --config "$2"
