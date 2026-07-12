#!/usr/bin/env bash

set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$project_root/build-sanitizer"
cmake -S "$project_root" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DDATASTREAM_BUILD_TESTS=ON \
    -DDATASTREAM_ENABLE_INTEGRATION_TESTS=OFF \
    -DDATASTREAM_ENABLE_SANITIZERS=ON
cmake --build "$build_dir" --parallel "${BUILD_JOBS:-2}"
ASAN_OPTIONS="detect_leaks=0:halt_on_error=1" \
UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" \
    ctest --test-dir "$build_dir" --output-on-failure
