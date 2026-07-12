#!/usr/bin/env python3
"""将固定三线业务样本逐一上传归档并核对文件与 MySQL 元数据。"""

import hashlib
import json
import os
import signal
import subprocess
import sys
import tempfile
from pathlib import Path

from upload_e2e_test import (
    assert_code,
    free_port,
    mysql_execute,
    redis,
    request,
    signed_headers,
    wait_ready,
)


def load_samples(run_root):
    """按清单顺序读取全部固定样本。"""
    samples = []
    for manifest_path in sorted((run_root / "_manifests").glob("*.json")):
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        for item in manifest["files"]:
            sample = dict(item)
            sample["path"] = run_root / item["relative_path"]
            samples.append(sample)
    return samples


def main():
    """运行全部样本的创建、分片、完成、详情和正文摘要闭环。"""
    server = Path(sys.argv[1]).resolve()
    source_config = Path(sys.argv[2]).resolve()
    run_root = Path(sys.argv[3]).resolve()
    environment = os.environ.copy()
    environment.setdefault("SMT_DATASTREAM_MYSQL_PASSWORD", "123456")
    environment.setdefault("SMT_DATASTREAM_OPERATOR_TOKEN", "e2e-operator-token-2026")
    samples = load_samples(run_root)
    assert samples
    assert sum(1 for item in samples if item["file_type"] == "RUNTIME_LOG") > 0
    assert {item["file_type"] for item in samples} == {
        "DETECTION_RESULT", "TEST_REPORT", "NG_IMAGE", "DEVICE_EXPORT", "RUNTIME_LOG"
    }

    with tempfile.TemporaryDirectory(prefix="datastream-mock-archive-e2e-") as temporary:
        root = Path(temporary)
        config = json.loads(source_config.read_text(encoding="utf-8"))
        config["http"]["port"] = free_port()
        config["redis"]["key_prefix"] = f"smt:test:mock-archive:{os.getpid()}:"
        config["upload"]["temp_root"] = str(root / "upload")
        config["upload"]["archive_root"] = str(root / "archive")
        config["upload"]["min_free_space_bytes"] = 0
        config["upload"]["min_free_space_percent"] = 0
        config["logging"]["file"] = str(root / "datastream.log")
        config_path = root / "config.json"
        config_path.write_text(json.dumps(config), encoding="utf-8")
        process = subprocess.Popen(
            [str(server), "--config", str(config_path)],
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        upload_ids = []
        try:
            wait_ready(config["http"]["port"], process)
            operator_headers = {
                "Authorization": f"Bearer {environment['SMT_DATASTREAM_OPERATOR_TOKEN']}"
            }
            for index, sample in enumerate(samples):
                content = sample["path"].read_bytes()
                assert len(content) == sample["file_size"]
                assert hashlib.sha256(content).hexdigest() == sample["sha256"]
                body = json.dumps(
                    {
                        "station_id": sample["station_id"],
                        "collector_id": sample["collector_id"],
                        "work_order": sample["work_order"],
                        "product_sn": sample["product_sn"],
                        "file_type": sample["file_type"],
                        "result": sample["result"],
                        "original_filename": sample["path"].name,
                        "file_size": len(content),
                        "file_sha256": sample["sha256"],
                        "chunk_size": 1048576,
                        "produced_at": sample["generated_at"],
                    },
                    separators=(",", ":"),
                ).encode()
                create_path = "/api/v1/uploads"
                secret = "smt-dev-" + sample["device_id"].lower()
                created = request(
                    config["http"]["port"], "POST", create_path, body,
                    signed_headers(
                        "POST", create_path, sample["device_id"], secret, body,
                        f"MOCK_CREATE_{index:04d}", "application/json"
                    ),
                )
                assert_code(created, 201, "OK")
                upload_id = created[1]["data"]["upload_id"]
                upload_ids.append(upload_id)
                chunks = [content[offset:offset + 1048576]
                          for offset in range(0, len(content), 1048576)]
                for chunk_no, chunk in enumerate(chunks):
                    chunk_path = f"/api/v1/uploads/{upload_id}/chunks/{chunk_no}"
                    uploaded = request(
                        config["http"]["port"], "PUT", chunk_path, chunk,
                        signed_headers(
                            "PUT", chunk_path, sample["device_id"], secret, chunk,
                            f"MOCK_CHUNK_{index:04d}_{chunk_no:03d}",
                            "application/octet-stream"
                        ),
                    )
                    assert_code(uploaded, 200, "OK")
                complete_path = f"/api/v1/uploads/{upload_id}/complete"
                completed = request(
                    config["http"]["port"], "POST", complete_path, b"",
                    signed_headers(
                        "POST", complete_path, sample["device_id"], secret, b"",
                        f"MOCK_COMPLETE_{index:04d}"
                    ),
                )
                assert_code(completed, 200, "OK")
                archive_id = completed[1]["data"]["archive_id"]
                detail = request(
                    config["http"]["port"], "GET", f"/api/v1/archives/{archive_id}", b"",
                    operator_headers,
                )
                assert_code(detail, 200, "OK")
                assert detail[1]["data"]["file_size"] == sample["file_size"]
                assert detail[1]["data"]["file_sha256"] == sample["sha256"]
                archived = root / "archive" / detail[1]["data"]["relative_path"]
                assert hashlib.sha256(archived.read_bytes()).hexdigest() == sample["sha256"]
        finally:
            process.send_signal(signal.SIGTERM)
            stdout, stderr = process.communicate(timeout=15)
            if upload_ids:
                identifiers = ",".join(f"'{upload_id}'" for upload_id in upload_ids)
                mysql_execute(
                    f"DELETE FROM archive_file WHERE upload_id IN ({identifiers})", environment
                )
            prefix = config["redis"]["key_prefix"]
            remaining = redis("--scan", "--pattern", prefix + "*")
            for key in remaining.splitlines():
                if key:
                    redis("DEL", key)
            if process.returncode != 0:
                raise AssertionError(
                    f"server exit={process.returncode} stdout={stdout} stderr={stderr}"
                )


if __name__ == "__main__":
    main()
