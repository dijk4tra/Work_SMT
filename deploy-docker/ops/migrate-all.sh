#!/usr/bin/env bash

set -euo pipefail

sql_string() {
    local value="$1"
    if [[ "$value" == *$'\n'* || "$value" == *$'\r'* ]]; then
        echo "database password must not contain a line break" >&2
        exit 1
    fi
    value="${value//\\/\\\\}"
    value="${value//\'/\'\'}"
    printf '%s' "$value"
}

mysql_root=(
    mysql --protocol=TCP --host=mysql --port=3306 --user=root
    --default-character-set=utf8mb4 --batch
)
export MYSQL_PWD="$SMT_MYSQL_ROOT_PASSWORD"

migrator_password="$(sql_string "$SMT_MYSQL_MIGRATOR_PASSWORD")"
datastream_password="$(sql_string "$SMT_DATASTREAM_MYSQL_PASSWORD")"
source_password="$(sql_string "$SMT_LOGTRACE_SOURCE_MYSQL_PASSWORD")"
state_password="$(sql_string "$SMT_LOGTRACE_STATE_MYSQL_PASSWORD")"

"${mysql_root[@]}" --execute="
    CREATE DATABASE IF NOT EXISTS smt_datastream
        CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;
    CREATE DATABASE IF NOT EXISTS smt_logtrace
        CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;
    CREATE USER IF NOT EXISTS 'smt_migrator'@'%' IDENTIFIED BY '$migrator_password';
    ALTER USER 'smt_migrator'@'%' IDENTIFIED BY '$migrator_password';
    CREATE USER IF NOT EXISTS 'smt_datastream'@'%' IDENTIFIED BY '$datastream_password';
    ALTER USER 'smt_datastream'@'%' IDENTIFIED BY '$datastream_password';
    CREATE USER IF NOT EXISTS 'smt_logtrace_reader'@'%' IDENTIFIED BY '$source_password';
    ALTER USER 'smt_logtrace_reader'@'%' IDENTIFIED BY '$source_password';
    CREATE USER IF NOT EXISTS 'smt_logtrace'@'%' IDENTIFIED BY '$state_password';
    ALTER USER 'smt_logtrace'@'%' IDENTIFIED BY '$state_password';
    GRANT ALL PRIVILEGES ON smt_datastream.* TO 'smt_migrator'@'%';
    GRANT ALL PRIVILEGES ON smt_logtrace.* TO 'smt_migrator'@'%';
    GRANT SELECT, INSERT, UPDATE, DELETE ON smt_datastream.* TO 'smt_datastream'@'%';
    GRANT SELECT ON smt_datastream.* TO 'smt_logtrace_reader'@'%';
    GRANT SELECT, INSERT, UPDATE, DELETE ON smt_logtrace.* TO 'smt_logtrace'@'%';
    FLUSH PRIVILEGES;"

jq '.mysql.user = "smt_migrator" |
    .mysql.password_env = "SMT_MYSQL_MIGRATOR_PASSWORD"' \
    /etc/smt/datastream.container.json > /tmp/datastream.migrate.json
jq '.source_mysql.password_env = "SMT_LOGTRACE_SOURCE_MYSQL_PASSWORD" |
    .state_mysql.user = "smt_migrator" |
    .state_mysql.password_env = "SMT_MYSQL_MIGRATOR_PASSWORD"' \
    /etc/smt/logtrace.container.json > /tmp/logtrace.migrate.json

/opt/smt/datastream/scripts/db.sh migrate --config /tmp/datastream.migrate.json
/opt/smt/logtrace/scripts/db.sh migrate --config /tmp/logtrace.migrate.json

if [[ "${SMT_APPLY_DEV_SEED:-0}" == "1" ]]; then
    /opt/smt/datastream/scripts/db.sh seed --config /tmp/datastream.migrate.json
    /opt/smt/logtrace/scripts/db.sh seed --config /tmp/logtrace.migrate.json
fi

echo "database users and migrations are ready"
