#!/usr/bin/env bash

set -euo pipefail

secret_file="${SMT_REDIS_PASSWORD_FILE:-/run/secrets/redis_password}"
if [[ ! -r "$secret_file" ]]; then
    echo "Redis password secret is not readable" >&2
    exit 1
fi
redis_password="$(<"$secret_file")"
if [[ -z "$redis_password" || "$redis_password" == *$'\n'* || "$redis_password" == *$'\r'* ]]; then
    echo "Redis password secret is empty or contains a line break" >&2
    exit 1
fi

umask 077
redis_password="${redis_password//\\/\\\\}"
redis_password="${redis_password//\"/\\\"}"
cp /opt/smt/redis.conf.template /run/redis/redis.conf
printf 'requirepass "%s"\n' "$redis_password" >> /run/redis/redis.conf
unset redis_password
exec redis-server /run/redis/redis.conf
