#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 1 || ! -d "$1" ]]; then
    echo "usage: restore-all.sh <backup-directory>" >&2
    exit 2
fi
backup_dir="$(realpath "$1")"
backup_name="$(basename "$backup_dir")"
if [[ "${SMT_RESTORE_CONFIRM:-}" != "RESTORE:$backup_name" ]]; then
    echo "set SMT_RESTORE_CONFIRM=RESTORE:$backup_name to confirm destructive restore" >&2
    exit 2
fi

/usr/local/bin/verify-backup.sh "$backup_dir"
export MYSQL_PWD="$SMT_MYSQL_ROOT_PASSWORD"
mysql_root=(mysql --protocol=TCP --host=mysql --port=3306 --user=root --batch)

"${mysql_root[@]}" --execute="
    DROP DATABASE IF EXISTS smt_logtrace;
    DROP DATABASE IF EXISTS smt_datastream;
    CREATE DATABASE smt_datastream CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;
    CREATE DATABASE smt_logtrace CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;"
"${mysql_root[@]}" smt_datastream < "$backup_dir/datastream.sql"
"${mysql_root[@]}" smt_logtrace < "$backup_dir/logtrace.sql"

mkdir -p /data/archive /data/upload_tmp /index
find /data/archive -mindepth 1 -delete
find /data/upload_tmp -mindepth 1 -delete
find /index -mindepth 1 -delete
tar -C /data/archive -xpf "$backup_dir/archive.tar"
tar -C /index -xpf "$backup_dir/index.tar"
chown -R 10001:10000 /data/archive /data/upload_tmp
chown -R 10002:10002 /index

redis_password="$SMT_REDIS_PASSWORD"
for prefix in 'smt:docker:datastream:*' 'smt:docker:logtrace:*'; do
    while IFS= read -r key; do
        [[ -n "$key" ]] || continue
        redis-cli --no-auth-warning -h redis -a "$redis_password" UNLINK "$key" >/dev/null
    done < <(redis-cli --no-auth-warning -h redis -a "$redis_password" --scan --pattern "$prefix")
done
unset redis_password

echo "restore completed; start applications and run readiness plus full integrity verification"
