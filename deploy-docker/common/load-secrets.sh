#!/usr/bin/env bash

set -euo pipefail

for variable_name in ${SMT_SECRET_VARS:-}; do
    file_variable_name="${variable_name}_FILE"
    secret_file="${!file_variable_name:-}"
    if [[ -z "$secret_file" || ! -r "$secret_file" ]]; then
        echo "required secret file is not readable: $file_variable_name" >&2
        exit 1
    fi
    secret_value="$(<"$secret_file")"
    if [[ -z "$secret_value" ]]; then
        echo "required secret file is empty: $file_variable_name" >&2
        exit 1
    fi
    export "$variable_name=$secret_value"
    unset "$file_variable_name" secret_value
done

exec "$@"
