#!/usr/bin/env bash

set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
required=(SMT_DATASTREAM_MYSQL_PASSWORD SMT_DATASTREAM_OPERATOR_TOKEN)
for name in "${required[@]}"; do
    if [[ -z "${!name:-}" ]]; then
        echo "environment variable $name is required" >&2
        exit 2
    fi
done

"$project_root/scripts/db.sh" migrate --config "$project_root/conf/datastream.json"
"$project_root/scripts/db.sh" seed --config "$project_root/conf/datastream.json"
"$project_root/scripts/build.sh" debug
ctest --test-dir "$project_root/build-debug" --output-on-failure
"$project_root/scripts/build.sh" release
ctest --test-dir "$project_root/build-release-clean" --output-on-failure
"$project_root/scripts/quality.sh"
"$project_root/scripts/coverage.sh"
"$project_root/scripts/sanitizer.sh"
"$project_root/scripts/valgrind.sh"
