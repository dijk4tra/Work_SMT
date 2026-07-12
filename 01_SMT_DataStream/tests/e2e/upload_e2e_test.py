#!/usr/bin/env python3
"""使用真实 HTTP、Redis、MySQL 和文件系统验证上传、归档与查询链路。"""

import hashlib
import hmac
import http.client
import json
import os
import signal
import socket
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from urllib.parse import urlencode


def redis(*arguments):
    """执行测试限定的 Redis 命令。"""
    return subprocess.run(
        ["redis-cli", "-h", "127.0.0.1", "-p", "6379", "--raw", *arguments],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


def mysql_execute(statement, environment):
    """执行测试限定的 MySQL 清理或故障窗口构造语句。"""
    mysql_environment = environment.copy()
    mysql_environment["MYSQL_PWD"] = environment["SMT_DATASTREAM_MYSQL_PASSWORD"]
    subprocess.run(
        ["mysql", "-h", "127.0.0.1", "-u", "root", "smt_datastream", "-e", statement],
        check=True,
        env=mysql_environment,
        capture_output=True,
        text=True,
    )


def free_port():
    """取得当前可绑定的回环端口。"""
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def signed_headers(method, path, device_id, secret_text, body, request_id, content_type=None):
    """生成指定设备请求的 v1 HMAC 请求头。"""
    timestamp = str(int(time.time()))
    digest = hashlib.sha256(body).hexdigest()
    canonical = "\n".join(["v1", method, path, device_id, timestamp, request_id, digest])
    secret = hashlib.sha256(secret_text.encode()).digest()
    signature = hmac.new(secret, canonical.encode(), hashlib.sha256).hexdigest()
    headers = {
        "X-Device-Id": device_id,
        "X-Timestamp": timestamp,
        "X-Request-Id": request_id,
        "X-Content-SHA256": digest,
        "X-Signature": signature,
    }
    if content_type:
        headers["Content-Type"] = content_type
    return headers


def request(port, method, path, body, headers):
    """调用 HTTP 接口并解析统一 JSON 响应。"""
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
    connection.request(method, path, body=body, headers=headers)
    response = connection.getresponse()
    payload = json.loads(response.read().decode())
    connection.close()
    return response.status, payload


def assert_code(result, status, code):
    """断言 HTTP 状态和稳定业务码。"""
    assert result[0] == status, result
    assert result[1]["code"] == code, result


def wait_ready(port, process):
    """等待临时服务开始监听。"""
    deadline = time.time() + 10
    while time.time() < deadline:
        if process.poll() is not None:
            raise AssertionError("datastream_server exited during startup")
        try:
            result = request(port, "GET", "/health/live", b"", {})
            if result[0] == 200:
                return
        except OSError:
            time.sleep(0.05)
    raise AssertionError("datastream_server startup timed out")


def create_body(file_size, digest, collector="IPC-L01-01"):
    """生成 AOI NG 图片上传元数据。"""
    return json.dumps(
        {
            "station_id": "ST-AOI-01",
            "collector_id": collector,
            "work_order": "WO-E2E-UPLOAD",
            "product_sn": "SN-E2E-UPLOAD-001",
            "file_type": "NG_IMAGE",
            "result": "NG",
            "original_filename": "board.png",
            "file_size": file_size,
            "file_sha256": digest,
            "chunk_size": 1048576,
            "produced_at": "2026-07-11T08:00:00.000+08:00",
        },
        separators=(",", ":"),
    ).encode()


def main():
    """运行分片、完成、幂等归档、查询、授权和配额用例。"""
    server = Path(sys.argv[1]).resolve()
    source_config = Path(sys.argv[2]).resolve()
    environment = os.environ.copy()
    environment.setdefault("SMT_DATASTREAM_MYSQL_PASSWORD", "123456")
    environment.setdefault("SMT_DATASTREAM_OPERATOR_TOKEN", "e2e-operator-token-2026")

    with tempfile.TemporaryDirectory(prefix="datastream-upload-e2e-") as temporary:
        root = Path(temporary)
        config = json.loads(source_config.read_text(encoding="utf-8"))
        config["http"]["port"] = free_port()
        config["redis"]["key_prefix"] = f"smt:test:upload:{os.getpid()}:"
        config["upload"]["temp_root"] = str(root / "upload")
        config["upload"]["archive_root"] = str(root / "archive")
        config["cleanup"]["interval_seconds"] = 1
        config["cleanup"]["expired_retention_seconds"] = 1
        config["logging"]["file"] = str(root / "datastream.log")
        config_path = root / "config.json"
        config_path.write_text(json.dumps(config), encoding="utf-8")
        port = config["http"]["port"]
        (root / "upload").mkdir()
        orphan = root / "upload" / "123e4567-e89b-42d3-a456-426614174099.part"
        unknown = root / "upload" / "manual.part"
        orphan.write_bytes(b"old orphan")
        unknown.write_bytes(b"must be preserved")
        os.utime(orphan, (1, 1))
        os.utime(unknown, (1, 1))
        process = subprocess.Popen(
            [str(server), "--config", str(config_path)],
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        upload_ids = []
        archive_ids = []
        try:
            wait_ready(port, process)
            cleanup_deadline = time.time() + 3
            while orphan.exists() and time.time() < cleanup_deadline:
                time.sleep(0.05)
            assert not orphan.exists()
            assert unknown.exists()
            content = (b"AOI-NG-E2E-" * 220000)[:2300000]
            digest = hashlib.sha256(content).hexdigest()
            create_path = "/api/v1/uploads"
            body = create_body(len(content), digest)
            headers = signed_headers(
                "POST", create_path, "AOI-VT-01", "smt-dev-aoi-vt-01", body,
                "UPLOAD_CREATE_E2E_0001", "application/json"
            )
            created = request(port, "POST", create_path, body, headers)
            assert_code(created, 201, "OK")
            upload_id = created[1]["data"]["upload_id"]
            upload_ids.append(upload_id)
            assert created[1]["data"]["chunk_count"] == 3

            progress_path = f"/api/v1/uploads/{upload_id}"
            progress_headers = signed_headers(
                "GET", progress_path, "AOI-VT-01", "smt-dev-aoi-vt-01", b"",
                "UPLOAD_PROGRESS_E2E_001"
            )
            progress = request(port, "GET", progress_path, b"", progress_headers)
            assert_code(progress, 200, "OK")
            assert progress[1]["data"]["missing_chunks"] == [0, 1, 2]

            chunks = [content[:1048576], content[1048576:2097152], content[2097152:]]
            for sequence, chunk_no in enumerate([2, 0, 1]):
                path = f"/api/v1/uploads/{upload_id}/chunks/{chunk_no}"
                chunk_headers = signed_headers(
                    "PUT", path, "AOI-VT-01", "smt-dev-aoi-vt-01", chunks[chunk_no],
                    f"UPLOAD_CHUNK_E2E_{sequence:04d}", "application/octet-stream"
                )
                result = request(port, "PUT", path, chunks[chunk_no], chunk_headers)
                assert_code(result, 200, "OK")
                assert result[1]["data"]["already_completed"] is False

            duplicate_path = f"/api/v1/uploads/{upload_id}/chunks/0"
            duplicate_headers = signed_headers(
                "PUT", duplicate_path, "AOI-VT-01", "smt-dev-aoi-vt-01", chunks[0],
                "UPLOAD_DUPLICATE_E2E_01", "application/octet-stream"
            )
            duplicate = request(port, "PUT", duplicate_path, chunks[0], duplicate_headers)
            assert_code(duplicate, 200, "OK")
            assert duplicate[1]["data"]["already_completed"] is True

            conflicting = b"X" + chunks[0][1:]
            conflict_headers = signed_headers(
                "PUT", duplicate_path, "AOI-VT-01", "smt-dev-aoi-vt-01", conflicting,
                "UPLOAD_CONFLICT_E2E_001", "application/octet-stream"
            )
            assert_code(
                request(port, "PUT", duplicate_path, conflicting, conflict_headers),
                409,
                "CHUNK_CONTENT_CONFLICT",
            )

            final_headers = signed_headers(
                "GET", progress_path, "AOI-VT-01", "smt-dev-aoi-vt-01", b"",
                "UPLOAD_PROGRESS_E2E_002"
            )
            final_progress = request(port, "GET", progress_path, b"", final_headers)
            assert_code(final_progress, 200, "OK")
            assert final_progress[1]["data"]["completed_chunks"] == [0, 1, 2]
            assert final_progress[1]["data"]["missing_chunks"] == []
            temporary_file = root / "upload" / f"{upload_id}.part"
            assert hashlib.sha256(temporary_file.read_bytes()).hexdigest() == digest

            wrong_headers = signed_headers(
                "GET", progress_path, "SPI-ZM-01", "smt-dev-spi-zm-01", b"",
                "UPLOAD_WRONG_DEVICE_001"
            )
            assert_code(request(port, "GET", progress_path, b"", wrong_headers), 403,
                        "UPLOAD_DEVICE_MISMATCH")

            bad_binding_body = create_body(len(content), digest, "IPC-L02-01")
            bad_binding_headers = signed_headers(
                "POST", create_path, "AOI-VT-01", "smt-dev-aoi-vt-01", bad_binding_body,
                "UPLOAD_BAD_BINDING_0001", "application/json"
            )
            assert_code(request(port, "POST", create_path, bad_binding_body, bad_binding_headers),
                        403, "COLLECTOR_DEVICE_MISMATCH")

            second_headers = signed_headers(
                "POST", create_path, "AOI-VT-01", "smt-dev-aoi-vt-01", body,
                "UPLOAD_CREATE_E2E_0002", "application/json"
            )
            second = request(port, "POST", create_path, body, second_headers)
            assert_code(second, 201, "OK")
            second_id = second[1]["data"]["upload_id"]
            upload_ids.append(second_id)
            concurrent_requests = []
            for chunk_no in [0, 1]:
                path = f"/api/v1/uploads/{second_id}/chunks/{chunk_no}"
                concurrent_headers = signed_headers(
                    "PUT", path, "AOI-VT-01", "smt-dev-aoi-vt-01", chunks[chunk_no],
                    f"UPLOAD_CONCURRENT_E2E_{chunk_no:02d}", "application/octet-stream"
                )
                concurrent_requests.append((path, chunks[chunk_no], concurrent_headers))
            with ThreadPoolExecutor(max_workers=2) as executor:
                concurrent_results = list(
                    executor.map(
                        lambda item: request(port, "PUT", item[0], item[1], item[2]),
                        concurrent_requests,
                    )
                )
            for concurrent_result in concurrent_results:
                assert_code(concurrent_result, 200, "OK")
            third_headers = signed_headers(
                "POST", create_path, "AOI-VT-01", "smt-dev-aoi-vt-01", body,
                "UPLOAD_CREATE_E2E_0003", "application/json"
            )
            assert_code(request(port, "POST", create_path, body, third_headers), 429,
                        "UPLOAD_LIMIT_EXCEEDED")

            incomplete_path = f"/api/v1/uploads/{second_id}/complete"
            incomplete_headers = signed_headers(
                "POST", incomplete_path, "AOI-VT-01", "smt-dev-aoi-vt-01", b"",
                "UPLOAD_COMPLETE_INCOMPLETE_01"
            )
            assert_code(request(port, "POST", incomplete_path, b"", incomplete_headers), 409,
                        "CHUNKS_INCOMPLETE")

            complete_path = f"/api/v1/uploads/{upload_id}/complete"
            complete_headers = signed_headers(
                "POST", complete_path, "AOI-VT-01", "smt-dev-aoi-vt-01", b"",
                "UPLOAD_COMPLETE_E2E_0001"
            )
            completed = request(port, "POST", complete_path, b"", complete_headers)
            assert_code(completed, 200, "OK")
            archive_id = completed[1]["data"]["archive_id"]
            archive_ids.append(archive_id)
            assert not temporary_file.exists()

            mysql_execute(
                f"DELETE FROM archive_file WHERE archive_id={archive_id}", environment
            )
            redis("HSET", config["redis"]["key_prefix"] + "upload:" + upload_id,
                  "state", "VERIFYING")

            repeat_headers = signed_headers(
                "POST", complete_path, "AOI-VT-01", "smt-dev-aoi-vt-01", b"",
                "UPLOAD_COMPLETE_E2E_0002"
            )
            repeated = request(port, "POST", complete_path, b"", repeat_headers)
            assert_code(repeated, 200, "OK")
            archive_id = repeated[1]["data"]["archive_id"]
            archive_ids.append(archive_id)

            redis("HSET", config["redis"]["key_prefix"] + "upload:" + upload_id,
                  "state", "VERIFYING")
            redis_repair_headers = signed_headers(
                "POST", complete_path, "AOI-VT-01", "smt-dev-aoi-vt-01", b"",
                "UPLOAD_COMPLETE_E2E_0004"
            )
            redis_repaired = request(
                port, "POST", complete_path, b"", redis_repair_headers
            )
            assert_code(redis_repaired, 200, "OK")
            assert redis_repaired[1]["data"]["archive_id"] == archive_id

            final_chunk_path = f"/api/v1/uploads/{second_id}/chunks/2"
            final_chunk_headers = signed_headers(
                "PUT", final_chunk_path, "AOI-VT-01", "smt-dev-aoi-vt-01", chunks[2],
                "UPLOAD_CONCURRENT_E2E_02", "application/octet-stream"
            )
            assert_code(request(port, "PUT", final_chunk_path, chunks[2], final_chunk_headers),
                        200, "OK")
            second_complete_headers = signed_headers(
                "POST", incomplete_path, "AOI-VT-01", "smt-dev-aoi-vt-01", b"",
                "UPLOAD_COMPLETE_E2E_0003"
            )
            second_completed = request(
                port, "POST", incomplete_path, b"", second_complete_headers
            )
            assert_code(second_completed, 200, "OK")
            second_archive_id = second_completed[1]["data"]["archive_id"]
            archive_ids.append(second_archive_id)

            query_path = "/api/v1/archives?" + urlencode(
                {
                    "device_id": "AOI-VT-01",
                    "work_order": "WO-E2E-UPLOAD",
                    "archived_from": "2026-07-01T00:00:00.000Z",
                    "archived_to": "2026-07-31T23:59:59.999Z",
                    "page_size": "1",
                }
            )
            operator_headers = {
                "Authorization": f"Bearer {environment['SMT_DATASTREAM_OPERATOR_TOKEN']}"
            }
            archives = request(port, "GET", query_path, b"", operator_headers)
            assert_code(archives, 200, "OK")
            assert len(archives[1]["data"]["items"]) == 1
            assert archives[1]["data"]["next_cursor"] is not None
            first_page_id = archives[1]["data"]["items"][0]["archive_id"]
            next_path = query_path + "&" + urlencode(
                {"cursor": archives[1]["data"]["next_cursor"]}
            )
            next_page = request(port, "GET", next_path, b"", operator_headers)
            assert_code(next_page, 200, "OK")
            assert len(next_page[1]["data"]["items"]) == 1, next_page
            second_page_id = next_page[1]["data"]["items"][0]["archive_id"]
            assert {first_page_id, second_page_id} == {archive_id, second_archive_id}
            assert next_page[1]["data"]["next_cursor"] is None

            detail_path = f"/api/v1/archives/{archive_id}"
            detail = request(port, "GET", detail_path, b"", operator_headers)
            assert_code(detail, 200, "OK")
            assert detail[1]["data"]["upload_id"] == upload_id
            assert detail[1]["data"]["file_sha256"] == digest
            archived_file = root / "archive" / detail[1]["data"]["relative_path"]
            assert hashlib.sha256(archived_file.read_bytes()).hexdigest() == digest

            mismatch_body = create_body(len(content), "0" * 64)
            mismatch_created = request(
                port, "POST", create_path, mismatch_body,
                signed_headers(
                    "POST", create_path, "AOI-VT-01", "smt-dev-aoi-vt-01", mismatch_body,
                    "UPLOAD_MISMATCH_CREATE_01", "application/json"
                ),
            )
            assert_code(mismatch_created, 201, "OK")
            mismatch_id = mismatch_created[1]["data"]["upload_id"]
            upload_ids.append(mismatch_id)
            for chunk_no, chunk in enumerate(chunks):
                mismatch_chunk_path = f"/api/v1/uploads/{mismatch_id}/chunks/{chunk_no}"
                mismatch_chunk = request(
                    port, "PUT", mismatch_chunk_path, chunk,
                    signed_headers(
                        "PUT", mismatch_chunk_path, "AOI-VT-01", "smt-dev-aoi-vt-01", chunk,
                        f"UPLOAD_MISMATCH_CHUNK_{chunk_no:02d}", "application/octet-stream"
                    ),
                )
                assert_code(mismatch_chunk, 200, "OK")
            mismatch_complete_path = f"/api/v1/uploads/{mismatch_id}/complete"
            mismatch_complete = request(
                port, "POST", mismatch_complete_path, b"",
                signed_headers(
                    "POST", mismatch_complete_path, "AOI-VT-01", "smt-dev-aoi-vt-01", b"",
                    "UPLOAD_MISMATCH_COMPLETE_01"
                ),
            )
            assert_code(mismatch_complete, 422, "FILE_INTEGRITY_MISMATCH")
            mismatch_progress_path = f"/api/v1/uploads/{mismatch_id}"
            mismatch_progress = request(
                port, "GET", mismatch_progress_path, b"",
                signed_headers(
                    "GET", mismatch_progress_path, "AOI-VT-01", "smt-dev-aoi-vt-01", b"",
                    "UPLOAD_MISMATCH_PROGRESS_01"
                ),
            )
            assert_code(mismatch_progress, 200, "OK")
            assert mismatch_progress[1]["data"]["state"] == "FAILED"
            assert mismatch_progress[1]["data"]["failure_code"] == "FILE_INTEGRITY_MISMATCH"
        finally:
            process.send_signal(signal.SIGTERM)
            stdout, stderr = process.communicate(timeout=10)
            if process.returncode != 0:
                raise AssertionError(
                    f"server exit={process.returncode} stdout={stdout} stderr={stderr}"
                )
            prefix = config["redis"]["key_prefix"]
            for archive_id in archive_ids:
                mysql_execute(f"DELETE FROM archive_file WHERE archive_id={archive_id}",
                              environment)
            for upload_id in upload_ids:
                redis("DEL", prefix + "upload:" + upload_id)
                redis("DEL", prefix + "upload:" + upload_id + ":chunks")
                redis("DEL", prefix + "upload:" + upload_id + ":digests")
                member = upload_id + ":2300000"
                redis("ZREM", prefix + "upload:quota:global", member)
                redis("ZREM", prefix + "upload:quota:device:AOI-VT-01", member)
                redis("ZREM", prefix + "upload:quota:collector:IPC-L01-01", member)
            for request_id in [
                "UPLOAD_CREATE_E2E_0001", "UPLOAD_PROGRESS_E2E_001",
                "UPLOAD_CHUNK_E2E_0000", "UPLOAD_CHUNK_E2E_0001",
                "UPLOAD_CHUNK_E2E_0002", "UPLOAD_DUPLICATE_E2E_01",
                "UPLOAD_CONFLICT_E2E_001", "UPLOAD_PROGRESS_E2E_002",
                "UPLOAD_WRONG_DEVICE_001", "UPLOAD_BAD_BINDING_0001",
                "UPLOAD_CREATE_E2E_0002", "UPLOAD_CREATE_E2E_0003",
                "UPLOAD_CONCURRENT_E2E_00", "UPLOAD_CONCURRENT_E2E_01",
                "UPLOAD_COMPLETE_INCOMPLETE_01", "UPLOAD_COMPLETE_E2E_0001",
                "UPLOAD_COMPLETE_E2E_0002", "UPLOAD_CONCURRENT_E2E_02",
                "UPLOAD_COMPLETE_E2E_0003", "UPLOAD_COMPLETE_E2E_0004",
                "UPLOAD_MISMATCH_CREATE_01", "UPLOAD_MISMATCH_CHUNK_00",
                "UPLOAD_MISMATCH_CHUNK_01", "UPLOAD_MISMATCH_CHUNK_02",
                "UPLOAD_MISMATCH_COMPLETE_01", "UPLOAD_MISMATCH_PROGRESS_01",
            ]:
                redis("DEL", prefix + "auth:req:AOI-VT-01:" + request_id)
                redis("DEL", prefix + "auth:req:SPI-ZM-01:" + request_id)
            remaining = redis("--scan", "--pattern", prefix + "*")
            for key in remaining.splitlines():
                if key:
                    redis("DEL", key)


if __name__ == "__main__":
    main()
