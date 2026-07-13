#!/usr/bin/env bash

set -euo pipefail

if [[ $# -gt 1 || "${1:-debug}" != "debug" && "${1:-debug}" != "release" ]]; then
    echo "usage: scripts/build.sh [debug|release]" >&2
    exit 2
fi

mode="${1:-debug}"
project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ "$mode" == "debug" ]]; then
    build_type="Debug"
    build_dir="$project_root/build"
else
    build_type="Release"
    build_dir="$project_root/build-release"
fi

cmake -S "$project_root" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE="$build_type" \
    -DLOGTRACE_BUILD_TESTS=ON \
    -DLOGTRACE_ENABLE_INTEGRATION_TESTS=ON
cmake --build "$build_dir" --parallel "${BUILD_JOBS:-2}"
