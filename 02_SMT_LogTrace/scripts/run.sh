#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: scripts/run.sh <search|gateway> <config-path>" >&2
    exit 2
fi

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
case "$1" in
    search) binary="$project_root/build/logsearch_server" ;;
    gateway) binary="$project_root/build/logtrace_gateway" ;;
    *) echo "component must be search or gateway" >&2; exit 2 ;;
esac

exec "$binary" --config "$2"
