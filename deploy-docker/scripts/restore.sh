#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 1 || ! "$1" =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$ ]]; then
    echo "usage: SMT_RESTORE_CONFIRM=RESTORE:<backup-id> scripts/restore.sh <backup-id>" >&2
    exit 2
fi
backup_id="$1"
if [[ "${SMT_RESTORE_CONFIRM:-}" != "RESTORE:$backup_id" ]]; then
    echo "restore confirmation is missing; no state was changed" >&2
    exit 2
fi
deploy_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$deploy_root"

docker compose stop --timeout 35 nginx logtrace-gateway logsearch datastream
docker compose --profile ops run --rm restore \
    /usr/local/bin/restore-all.sh "/backup/$backup_id"
docker compose up -d datastream logsearch logtrace-gateway nginx
docker compose --profile ops run --rm integrity \
    /usr/local/bin/integrity-check.sh --full
echo "restore and full integrity verification completed: $backup_id"
