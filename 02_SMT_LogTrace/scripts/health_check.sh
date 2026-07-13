#!/usr/bin/env bash

set -euo pipefail

base_url="${1:-http://127.0.0.1:8081}"
curl --fail --silent --show-error "$base_url/health/live"
echo
curl --fail --silent --show-error "$base_url/health/ready"
echo
