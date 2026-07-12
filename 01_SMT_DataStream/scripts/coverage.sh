#!/usr/bin/env bash

set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$project_root/build-coverage"
cmake -S "$project_root" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DDATASTREAM_BUILD_TESTS=ON \
    -DDATASTREAM_ENABLE_INTEGRATION_TESTS=ON \
    -DDATASTREAM_ENABLE_COVERAGE=ON
cmake --build "$build_dir" --parallel "${BUILD_JOBS:-2}"
lcov --directory "$build_dir" --zerocounters --quiet
ctest --test-dir "$build_dir" --output-on-failure
lcov --capture --directory "$build_dir" --output-file "$build_dir/all.info" --quiet
lcov --remove "$build_dir/all.info" '/usr/*' '*/tests/*' \
    --output-file "$build_dir/core.info" --quiet
line_rate="$(lcov --summary "$build_dir/core.info" 2>&1 | sed -n 's/.*lines......: \([0-9.]*\)%.*/\1/p')"
awk -v rate="$line_rate" 'BEGIN { if (rate + 0 < 80) exit 1 }'
echo "core line coverage passed: ${line_rate}%"
