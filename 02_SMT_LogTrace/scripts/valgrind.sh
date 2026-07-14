#!/usr/bin/env bash

set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$project_root/build-valgrind"
cmake -S "$project_root" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DLOGTRACE_BUILD_TESTS=ON \
    -DLOGTRACE_ENABLE_INTEGRATION_TESTS=OFF
cmake --build "$build_dir" --parallel "${BUILD_JOBS:-2}"
valgrind --error-exitcode=1 --leak-check=full --show-leak-kinds=all \
    "$build_dir/tests/logtrace_unit_tests"
