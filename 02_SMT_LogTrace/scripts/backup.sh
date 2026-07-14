#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 3 || "$1" != "--config" ]]; then
    echo "usage: scripts/backup.sh --config <path> <empty-output-directory>" >&2
    exit 2
fi

config_path="$2"
output_dir="$3"
if [[ -e "$output_dir" ]]; then
    echo "output directory already exists: $output_dir" >&2
    exit 1
fi
password_env="$(jq -er '.state_mysql.password_env' "$config_path")"
if [[ ! "$password_env" =~ ^[A-Za-z_][A-Za-z0-9_]*$ || -z "${!password_env:-}" ]]; then
    echo "configured state MySQL password environment variable is required" >&2
    exit 2
fi

host="$(jq -er '.state_mysql.host' "$config_path")"
port="$(jq -er '.state_mysql.port' "$config_path")"
user="$(jq -er '.state_mysql.user' "$config_path")"
database="$(jq -er '.state_mysql.database' "$config_path")"
index_root="$(realpath "$(jq -er '.storage.index_root' "$config_path")")"
mkdir -m 0700 "$output_dir"
export MYSQL_PWD="${!password_env}"
mysqldump --protocol=TCP --host="$host" --port="$port" --user="$user" \
    --single-transaction --routines=false --triggers "$database" >"$output_dir/state_mysql.sql"
tar -C "$index_root" -cpf "$output_dir/index.tar" .
sha256sum "$output_dir/state_mysql.sql" "$output_dir/index.tar" >"$output_dir/SHA256SUMS"
echo "backup created: $output_dir"
