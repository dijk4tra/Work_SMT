#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: scripts/verify_backup.sh <backup-directory>" >&2
    exit 2
fi

backup_dir="$1"
(cd "$backup_dir" && sha256sum --check SHA256SUMS)
tar -tf "$backup_dir/index.tar" >/dev/null
grep -q 'CREATE TABLE `index_batch`' "$backup_dir/state_mysql.sql"
echo "backup structure and checksums verified: $backup_dir"
