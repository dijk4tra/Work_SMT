#!/usr/bin/env bash

set -euo pipefail

mode="${1:---metadata}"
if [[ "$mode" != "--metadata" && "$mode" != "--full" ]]; then
    echo "usage: scripts/integrity-check.sh [--metadata|--full]" >&2
    exit 2
fi
deploy_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$deploy_root"
docker compose --profile ops run --rm integrity \
    /usr/local/bin/integrity-check.sh "$mode"
