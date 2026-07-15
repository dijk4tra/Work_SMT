#!/usr/bin/env bash

set -euo pipefail

mode="${1:---metadata}"
if [[ "$mode" != "--metadata" && "$mode" != "--full" ]]; then
    echo "usage: integrity-check.sh [--metadata|--full]" >&2
    exit 2
fi

failures=0
archive_count=0
segment_count=0
export MYSQL_PWD="$SMT_LOGTRACE_SOURCE_MYSQL_PASSWORD"
mysql --protocol=TCP --host=mysql --port=3306 --user=smt_logtrace_reader \
    --database=smt_datastream --batch --skip-column-names --raw \
    --execute='SELECT archive_id, relative_path, file_size, LOWER(HEX(file_sha256))
               FROM archive_file ORDER BY archive_id' > /tmp/archives.tsv

while IFS=$'\t' read -r archive_id relative_path expected_size expected_sha; do
    [[ -n "$archive_id" ]] || continue
    archive_count=$((archive_count + 1))
    candidate="$(realpath -m "/data/archive/$relative_path")"
    if [[ "$candidate" != /data/archive/* || ! -f "$candidate" || -L "$candidate" ]]; then
        echo "archive $archive_id is missing or unsafe: $relative_path" >&2
        failures=$((failures + 1))
        continue
    fi
    actual_size="$(stat --format='%s' "$candidate")"
    if [[ "$actual_size" != "$expected_size" ]]; then
        echo "archive $archive_id size mismatch" >&2
        failures=$((failures + 1))
        continue
    fi
    if [[ "$mode" == "--full" ]]; then
        actual_sha="$(sha256sum "$candidate" | awk '{print $1}')"
        if [[ "$actual_sha" != "$expected_sha" ]]; then
            echo "archive $archive_id SHA-256 mismatch" >&2
            failures=$((failures + 1))
        fi
    fi
done < /tmp/archives.tsv

export MYSQL_PWD="$SMT_LOGTRACE_STATE_MYSQL_PASSWORD"
mysql --protocol=TCP --host=mysql --port=3306 --user=smt_logtrace \
    --database=smt_logtrace --batch --skip-column-names --raw \
    --execute="SELECT batch_id, segment_name, LOWER(HEX(segment_sha256))
               FROM index_batch WHERE state='READY' ORDER BY batch_id" > /tmp/segments.tsv

while IFS=$'\t' read -r batch_id segment_name expected_manifest_sha; do
    [[ -n "$batch_id" ]] || continue
    segment_count=$((segment_count + 1))
    if [[ ! "$segment_name" =~ ^segment_[0-9]+$ ]]; then
        echo "READY batch $batch_id has unsafe segment name" >&2
        failures=$((failures + 1))
        continue
    fi
    segment_dir="/index/segments/$segment_name"
    manifest="$segment_dir/manifest.json"
    if [[ ! -f "$manifest" || -L "$manifest" ]]; then
        echo "READY batch $batch_id manifest is missing" >&2
        failures=$((failures + 1))
        continue
    fi
    actual_manifest_sha="$(sha256sum "$manifest" | awk '{print $1}')"
    if [[ "$actual_manifest_sha" != "$expected_manifest_sha" ]]; then
        echo "READY batch $batch_id manifest SHA-256 mismatch" >&2
        failures=$((failures + 1))
        continue
    fi
    if ! jq -e '
        .artifacts as $artifacts |
        ($artifacts | type == "object") and
        (($artifacts | keys | sort) ==
            ["documents.bin", "files.bin", "postings.bin", "terms.bin"]) and
        ($artifacts | all(.[];
            (.size | type == "number") and (.size >= 0) and (.size == (.size | floor)) and
            (.sha256 | type == "string") and
            (.sha256 | test("^[0-9a-fA-F]{64}$"))))
        ' "$manifest" >/dev/null; then
        echo "READY batch $batch_id manifest is invalid" >&2
        failures=$((failures + 1))
        continue
    fi
    while IFS=$'\t' read -r artifact expected_size expected_sha; do
        case "$artifact" in
            documents.bin|files.bin|postings.bin|terms.bin) ;;
            *) echo "READY batch $batch_id has unknown artifact" >&2; failures=$((failures + 1)); continue ;;
        esac
        artifact_path="$segment_dir/$artifact"
        if [[ ! -f "$artifact_path" || -L "$artifact_path" \
            || "$(stat --format='%s' "$artifact_path")" != "$expected_size" ]]; then
            echo "READY batch $batch_id artifact size mismatch: $artifact" >&2
            failures=$((failures + 1))
            continue
        fi
        if [[ "$mode" == "--full" && "$(sha256sum "$artifact_path" | awk '{print $1}')" != "$expected_sha" ]]; then
            echo "READY batch $batch_id artifact SHA-256 mismatch: $artifact" >&2
            failures=$((failures + 1))
        fi
    done < <(jq -r '.artifacts | to_entries[] |
        [.key, (.value.size | tostring), .value.sha256] | @tsv' "$manifest")
done < /tmp/segments.tsv

if (( failures > 0 )); then
    echo "integrity check failed: archives=$archive_count segments=$segment_count failures=$failures" >&2
    exit 1
fi
echo "integrity check passed: mode=$mode archives=$archive_count READY_segments=$segment_count"
