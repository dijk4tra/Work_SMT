#!/usr/bin/env python3
"""使用真实 HTTP、Redis、MySQL 和临时文件验证第三期上传链路。"""

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


def redis(*arguments):
    """执行测试限定的 Redis 命令。"""
    return subprocess.run(
        ["redis-cli", "-h", "127.0.0.1", "-p", "6379", "--raw", *arguments],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


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
    """运行创建、乱序、幂等、冲突、进度、授权和配额用例。"""
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
        config["logging"]["file"] = str(root / "datastream.log")
        config_path = root / "config.json"
        config_path.write_text(json.dumps(config), encoding="utf-8")
        port = config["http"]["port"]
        process = subprocess.Popen(
            [str(server), "--config", str(config_path)],
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        upload_ids = []
        try:
            wait_ready(port, process)
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
        finally:
            process.send_signal(signal.SIGTERM)
            stdout, stderr = process.communicate(timeout=10)
            if process.returncode != 0:
                raise AssertionError(
                    f"server exit={process.returncode} stdout={stdout} stderr={stderr}"
                )
            prefix = config["redis"]["key_prefix"]
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
            ]:
                redis("DEL", prefix + "auth:req:AOI-VT-01:" + request_id)
                redis("DEL", prefix + "auth:req:SPI-ZM-01:" + request_id)
            remaining = redis("--scan", "--pattern", prefix + "*")
            for key in remaining.splitlines():
                if key:
                    redis("DEL", key)


if __name__ == "__main__":
    main()
