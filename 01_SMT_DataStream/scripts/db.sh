#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 3 || "$2" != "--config" ]]; then
    echo "usage: scripts/db.sh <migrate|seed> --config <path>" >&2
    exit 2
fi

action="$1"
config_path="$3"
if [[ "$action" != "migrate" && "$action" != "seed" ]]; then
    echo "action must be migrate or seed" >&2
    exit 2
fi
if [[ ! -r "$config_path" ]]; then
    echo "config file is not readable: $config_path" >&2
    exit 2
fi

host="$(jq -er '.mysql.host' "$config_path")"
port="$(jq -er '.mysql.port' "$config_path")"
database="$(jq -er '.mysql.database' "$config_path")"
user="$(jq -er '.mysql.user' "$config_path")"
password_env="$(jq -er '.mysql.password_env' "$config_path")"
if [[ ! "$password_env" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]]; then
    echo "mysql.password_env is not a valid environment variable name" >&2
    exit 2
fi
password="${!password_env:-}"
if [[ -z "$password" ]]; then
    echo "environment variable $password_env is required" >&2
    exit 2
fi

export MYSQL_PWD="$password"
mysql_args=(
    --protocol=TCP
    --host="$host"
    --port="$port"
    --user="$user"
    --database="$database"
    --default-character-set=utf8mb4
    --show-warnings
    --batch
    --skip-column-names
)

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ "$action" == "seed" ]]; then
    table_count="$(mysql "${mysql_args[@]}" --execute="
        SELECT COUNT(*)
        FROM information_schema.tables
        WHERE table_schema = DATABASE() AND table_name = 'device';")"
    if [[ "$table_count" != "1" ]]; then
        echo "database schema is missing; run migrate first" >&2
        exit 1
    fi
    mysql "${mysql_args[@]}" < "$project_root/migrations/dev_seed.sql"
    echo "development seed applied"
    exit 0
fi

for migration in "$project_root"/migrations/[0-9][0-9][0-9]_*.sql; do
    filename="$(basename "$migration")"
    version="${filename%%_*}"
    checksum="$(sha256sum "$migration" | awk '{print $1}')"

    if [[ "$version" == "000" ]]; then
        migration_table_count="$(mysql "${mysql_args[@]}" --execute="
            SELECT COUNT(*)
            FROM information_schema.tables
            WHERE table_schema = DATABASE() AND table_name = 'schema_migration';")"
        if [[ "$migration_table_count" == "0" ]]; then
            mysql "${mysql_args[@]}" < "$migration"
        fi
    fi

    existing="$(mysql "${mysql_args[@]}" --execute="
        SELECT checksum FROM schema_migration WHERE version = '$version';")"
    if [[ -n "$existing" ]]; then
        if [[ "$existing" != "$checksum" ]]; then
            echo "migration checksum mismatch: $filename" >&2
            exit 1
        fi
        echo "migration already applied: $filename"
        continue
    fi

    if [[ "$version" != "000" ]]; then
        mysql "${mysql_args[@]}" < "$migration"
    fi
    mysql "${mysql_args[@]}" --execute="
        INSERT INTO schema_migration (version, checksum, applied_at)
        VALUES ('$version', '$checksum', UTC_TIMESTAMP(3));"
    echo "migration applied: $filename"
done
