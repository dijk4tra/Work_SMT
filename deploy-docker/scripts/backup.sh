#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 1 || ! "$1" =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$ ]]; then
    echo "usage: scripts/backup.sh <backup-id>" >&2
    exit 2
fi
backup_id="$1"
deploy_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$deploy_root"

restart_apps() {
    docker compose up -d datastream logsearch logtrace-gateway nginx >/dev/null
}
trap restart_apps EXIT

docker compose stop --timeout 35 nginx logtrace-gateway logsearch datastream
docker compose --profile ops run --rm backup \
    /usr/local/bin/backup-all.sh "/backup/$backup_id"

trap - EXIT
restart_apps
echo "consistent backup completed: $backup_id"
