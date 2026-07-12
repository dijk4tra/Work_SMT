#!/usr/bin/env bash

set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mapfile -t cpp_files < <(find "$project_root/include" "$project_root/src" "$project_root/tests" \
    -type f \( -name '*.h' -o -name '*.cpp' \) | sort)
clang-format --dry-run --Werror "${cpp_files[@]}"
mapfile -t sources < <(find "$project_root/src" -type f -name '*.cpp' | sort)
clang-tidy -p "$project_root/build" "${sources[@]}"
