#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 1 || ! -d "$1" ]]; then
    echo "usage: verify-backup.sh <backup-directory>" >&2
    exit 2
fi
backup_dir="$(realpath "$1")"

required_files=(datastream.sql logtrace.sql archive.tar index.tar manifest.json)
if [[ ! -f "$backup_dir/SHA256SUMS" ]]; then
    echo "backup checksum manifest is missing" >&2
    exit 1
fi
if [[ "$(wc -l < "$backup_dir/SHA256SUMS")" -ne "${#required_files[@]}" ]]; then
    echo "backup checksum manifest must contain exactly ${#required_files[@]} entries" >&2
    exit 1
fi
for file in "${required_files[@]}"; do
    if [[ ! -f "$backup_dir/$file" ]] \
        || [[ "$(awk -v expected="$file" '$2 == expected {count++} END {print count + 0}' \
            "$backup_dir/SHA256SUMS")" -ne 1 ]]; then
        echo "backup file or checksum entry is invalid: $file" >&2
        exit 1
    fi
done
(cd "$backup_dir" && sha256sum --strict --check SHA256SUMS)
grep -q 'CREATE TABLE `archive_file`' "$backup_dir/datastream.sql"
grep -q 'CREATE TABLE `index_batch`' "$backup_dir/logtrace.sql"

tar_list="$(mktemp)"
trap 'rm -f "$tar_list"' EXIT
for archive in archive.tar index.tar; do
    tar --list --file "$backup_dir/$archive" >"$tar_list"
    if awk '$0 ~ /^\// || $0 ~ /(^|\/)\.\.($|\/)/ {found=1}
        END {exit found ? 0 : 1}' "$tar_list"; then
        echo "unsafe path found in $archive" >&2
        exit 1
    fi
    tar --list --verbose --file "$backup_dir/$archive" >"$tar_list"
    if awk 'substr($0, 1, 1) != "-" && substr($0, 1, 1) != "d" {found=1}
        END {exit found ? 0 : 1}' "$tar_list"; then
        echo "links or special files are not allowed in $archive" >&2
        exit 1
    fi
done

jq -e '.format_version == 1 and .databases == ["smt_datastream", "smt_logtrace"]' \
    "$backup_dir/manifest.json" >/dev/null
echo "backup checksums and structure are valid: $backup_dir"
