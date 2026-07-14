#!/usr/bin/env bash

set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
required=(SMT_LOGTRACE_SOURCE_MYSQL_PASSWORD SMT_LOGTRACE_STATE_MYSQL_PASSWORD \
          SMT_LOGTRACE_OPERATOR_TOKEN)
for name in "${required[@]}"; do
    if [[ -z "${!name:-}" ]]; then
        echo "environment variable $name is required" >&2
        exit 2
    fi
done

"$project_root/scripts/db.sh" migrate --config "$project_root/conf/logtrace.json"
"$project_root/scripts/db.sh" seed --config "$project_root/conf/logtrace.json"
"$project_root/scripts/build.sh" debug
ctest --test-dir "$project_root/build" --output-on-failure
"$project_root/scripts/build.sh" release
ctest --test-dir "$project_root/build-release" --output-on-failure
cmake --build "$project_root/../01_SMT_DataStream/build" --parallel "${BUILD_JOBS:-2}"
python3 "$project_root/tests/e2e/cross_project_e2e_test.py" \
    "$project_root/../01_SMT_DataStream/build/datastream_server" \
    "$project_root/../01_SMT_DataStream/build/collector_agent" \
    "$project_root/../01_SMT_DataStream/conf/datastream.json" \
    "$project_root/../01_SMT_DataStream/scripts/db.sh" \
    "$project_root/build/logtrace_admin" "$project_root/build/logsearch_server" \
    "$project_root/build/logtrace_gateway" "$project_root/conf/logtrace.json" \
    "$project_root/scripts/db.sh"
"$project_root/build/tests/logtrace_search_load_test"
"$project_root/scripts/quality.sh"
"$project_root/scripts/coverage.sh"
"$project_root/scripts/sanitizer.sh"
"$project_root/scripts/valgrind.sh"
