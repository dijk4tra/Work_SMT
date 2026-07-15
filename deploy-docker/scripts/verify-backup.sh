#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 1 || ! "$1" =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$ ]]; then
    echo "usage: scripts/verify-backup.sh <backup-id>" >&2
    exit 2
fi
deploy_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$deploy_root"
docker compose --profile ops run --rm --no-deps backup \
    /usr/local/bin/verify-backup.sh "/backup/$1"
