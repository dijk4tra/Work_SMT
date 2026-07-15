#!/usr/bin/env bash

set -euo pipefail
umask 077

if [[ $# -ne 1 || -e "$1" ]]; then
    echo "usage: backup-all.sh <new-backup-directory>" >&2
    exit 2
fi
output_dir="$1"
mkdir -m 0700 "$output_dir"

export MYSQL_PWD="$SMT_MYSQL_MIGRATOR_PASSWORD"
mysql_args=(
    --protocol=TCP --host=mysql --port=3306 --user=smt_migrator
    --single-transaction --quick --routines=false --triggers
    --set-gtid-purged=OFF --no-tablespaces --default-character-set=utf8mb4
)

mysqldump "${mysql_args[@]}" smt_datastream > "$output_dir/datastream.sql"
mysqldump "${mysql_args[@]}" smt_logtrace > "$output_dir/logtrace.sql"
tar -C /data/archive -cpf "$output_dir/archive.tar" .
tar -C /index -cpf "$output_dir/index.tar" .

binlog_position="$(MYSQL_PWD="$SMT_MYSQL_ROOT_PASSWORD" mysql --protocol=TCP --host=mysql \
    --port=3306 --user=root --batch --skip-column-names --execute='SHOW MASTER STATUS' \
    | tr '\t' ' ' || true)"
jq -n \
    --arg created_at "$(date --utc +%Y-%m-%dT%H:%M:%SZ)" \
    --arg release "${SMT_RELEASE_VERSION:-unknown}" \
    --arg binlog_position "$binlog_position" \
    '{format_version: 1, created_at: $created_at, release: $release,
      databases: ["smt_datastream", "smt_logtrace"],
      mysql_binlog_position: $binlog_position,
      includes: ["DataStream archive", "LogTrace READY/index artifacts"],
      excludes: ["upload_tmp", "Redis session/cache state"]}' \
    > "$output_dir/manifest.json"

(
    cd "$output_dir"
    sha256sum datastream.sql logtrace.sql archive.tar index.tar manifest.json > SHA256SUMS
)
/usr/local/bin/verify-backup.sh "$output_dir"
