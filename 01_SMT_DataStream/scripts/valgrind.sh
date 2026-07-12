#!/usr/bin/env bash

set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cmake -S "$project_root" -B "$project_root/build-valgrind" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DDATASTREAM_BUILD_TESTS=ON \
    -DDATASTREAM_ENABLE_INTEGRATION_TESTS=OFF
cmake --build "$project_root/build-valgrind" --parallel "${BUILD_JOBS:-2}"
valgrind --error-exitcode=1 --leak-check=full --show-leak-kinds=all \
    "$project_root/build-valgrind/tests/datastream_unit_tests"
