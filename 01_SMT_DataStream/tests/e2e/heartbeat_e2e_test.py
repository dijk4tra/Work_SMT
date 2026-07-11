#!/usr/bin/env python3
"""使用真实 MySQL、Redis 和 HTTP 服务验证第二期心跳完整链路。"""

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
from pathlib import Path


def mysql(query):
    """执行固定测试 SQL 并返回标准输出。"""
    environment = os.environ.copy()
    environment["MYSQL_PWD"] = environment["SMT_DATASTREAM_MYSQL_PASSWORD"]
    result = subprocess.run(
        ["mysql", "-h", "127.0.0.1", "-uroot", "-Nse", query],
        check=True,
        capture_output=True,
        text=True,
        env=environment,
    )
    return result.stdout.strip()


def redis(*arguments):
    """执行固定测试 Redis 命令并返回标准输出。"""
    result = subprocess.run(
        ["redis-cli", "-h", "127.0.0.1", "-p", "6379", "--raw", *arguments],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def free_port():
    """取得当前可绑定的本机端口。"""
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def signed_headers(device_id, secret_text, body, request_id, timestamp=None):
    """按 API 契约生成设备认证请求头。"""
    timestamp_text = str(int(time.time()) if timestamp is None else timestamp)
    digest = hashlib.sha256(body).hexdigest()
    canonical = "\n".join(
        [
            "v1",
            "POST",
            "/api/v1/devices/heartbeat",
            device_id,
            timestamp_text,
            request_id,
            digest,
        ]
    )
    secret = hashlib.sha256(secret_text.encode()).digest()
    signature_value = hmac.new(secret, canonical.encode(), hashlib.sha256).hexdigest()
    return {
        "Content-Type": "application/json",
        "X-Device-Id": device_id,
        "X-Timestamp": timestamp_text,
        "X-Request-Id": request_id,
        "X-Content-SHA256": digest,
        "X-Signature": signature_value,
    }


def post(port, body, headers):
    """发送心跳并返回 HTTP 状态和 JSON 响应。"""
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
    connection.request("POST", "/api/v1/devices/heartbeat", body=body, headers=headers)
    response = connection.getresponse()
    payload = json.loads(response.read().decode())
    connection.close()
    return response.status, payload


def assert_code(result, status, code):
    """断言统一响应的 HTTP 状态和业务码。"""
    assert result[0] == status, result
    assert result[1]["code"] == code, result


def wait_until_ready(port, process):
    """等待服务进入可接收请求状态。"""
    deadline = time.time() + 10
    while time.time() < deadline:
        if process.poll() is not None:
            raise AssertionError("datastream_server exited during startup")
        try:
            connection = http.client.HTTPConnection("127.0.0.1", port, timeout=1)
            connection.request("GET", "/health/live")
            response = connection.getresponse()
            response.read()
            connection.close()
            if response.status == 200:
                return
        except OSError:
            time.sleep(0.05)
    raise AssertionError("datastream_server startup timed out")


def main():
    """运行心跳成功、认证失败、重放和状态写入用例。"""
    server_binary = Path(sys.argv[1]).resolve()
    source_config = Path(sys.argv[2]).resolve()
    environment = os.environ.copy()
    environment.setdefault("SMT_DATASTREAM_MYSQL_PASSWORD", "123456")
    environment.setdefault("SMT_DATASTREAM_OPERATOR_TOKEN", "e2e-operator-token-2026")
    os.environ.update(environment)

    disabled_device = "AOI-DISABLED-TEST"
    redis("DEL", "smt:datastream:auth:req:AOI-VT-01:E2E_VALID_REQUEST_0001")
    redis("DEL", "smt:datastream:auth:req:AOI-VT-01:E2E_INVALID_BODY_0001")
    mysql(
        "USE smt_datastream; INSERT INTO device "
        "(device_id,station_id,device_model,software_version,hmac_secret,enabled) VALUES "
        "('AOI-DISABLED-TEST','ST-AOI-01','E2E','1.0',"
        "UNHEX(SHA2('smt-dev-disabled-test',256)),0) AS new ON DUPLICATE KEY UPDATE enabled=0"
    )

    with tempfile.TemporaryDirectory(prefix="datastream-e2e-") as temporary:
        temporary_path = Path(temporary)
        config = json.loads(source_config.read_text(encoding="utf-8"))
        port = free_port()
        config["http"]["port"] = port
        config["upload"]["temp_root"] = str(temporary_path / "upload")
        config["upload"]["archive_root"] = str(temporary_path / "archive")
        config["logging"]["file"] = str(temporary_path / "datastream.log")
        config_path = temporary_path / "config.json"
        config_path.write_text(json.dumps(config), encoding="utf-8")

        process = subprocess.Popen(
            [str(server_binary), "--config", str(config_path)],
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        try:
            wait_until_ready(port, process)
            body = json.dumps(
                {
                    "collector_id": "IPC-L01-01",
                    "software_version": "5.4.8-e2e",
                    "runtime_status": "RUNNING",
                    "work_order": "WO-E2E-20260711",
                    "reported_at": "2026-07-11T08:00:00.000+08:00",
                },
                separators=(",", ":"),
            ).encode()

            assert_code(post(port, body, {}), 401, "AUTH_REQUIRED")

            valid_headers = signed_headers(
                "AOI-VT-01", "smt-dev-aoi-vt-01", body, "E2E_VALID_REQUEST_0001"
            )
            valid_result = post(port, body, valid_headers)
            assert_code(valid_result, 200, "OK")
            assert valid_result[1]["data"]["device_id"] == "AOI-VT-01"
            assert valid_result[1]["data"]["online"] is True
            assert_code(post(port, body, valid_headers), 409, "REQUEST_REPLAYED")

            tamper_headers = signed_headers(
                "AOI-VT-01", "smt-dev-aoi-vt-01", body, "E2E_TAMPER_REQUEST_01"
            )
            assert_code(post(port, body + b" ", tamper_headers), 401, "SIGNATURE_INVALID")

            expired_headers = signed_headers(
                "AOI-VT-01",
                "smt-dev-aoi-vt-01",
                body,
                "E2E_EXPIRED_REQUEST_1",
                int(time.time()) - 301,
            )
            assert_code(post(port, body, expired_headers), 401, "TIMESTAMP_EXPIRED")

            unknown_headers = signed_headers(
                "AOI-UNKNOWN-TEST", "unused", body, "E2E_UNKNOWN_REQUEST_1"
            )
            assert_code(post(port, body, unknown_headers), 404, "DEVICE_NOT_FOUND")

            disabled_headers = signed_headers(
                disabled_device, "smt-dev-disabled-test", body, "E2E_DISABLED_REQUEST1"
            )
            assert_code(post(port, body, disabled_headers), 403, "DEVICE_DISABLED")

            uppercase_headers = signed_headers(
                "AOI-VT-01", "smt-dev-aoi-vt-01", body, "E2E_UPPERCASE_REQUEST1"
            )
            uppercase_headers["X-Content-SHA256"] = uppercase_headers[
                "X-Content-SHA256"
            ].upper()
            assert_code(post(port, body, uppercase_headers), 401, "SIGNATURE_INVALID")

            invalid_body = b"{}"
            invalid_headers = signed_headers(
                "AOI-VT-01", "smt-dev-aoi-vt-01", invalid_body, "E2E_INVALID_BODY_0001"
            )
            assert_code(post(port, invalid_body, invalid_headers), 400, "INVALID_ARGUMENT")

            assert mysql(
                "SELECT software_version FROM smt_datastream.device "
                "WHERE device_id='AOI-VT-01'"
            ) == "5.4.8-e2e"
            assert mysql(
                "SELECT last_seen_at IS NOT NULL FROM smt_datastream.device "
                "WHERE device_id='AOI-VT-01'"
            ) == "1"
            heartbeat_key = "smt:datastream:heartbeat:AOI-VT-01"
            ttl = int(redis("TTL", heartbeat_key))
            assert 0 < ttl <= config["device"]["heartbeat_ttl_seconds"]
            assert redis("HGET", heartbeat_key, "runtime_status") == "RUNNING"
        finally:
            process.send_signal(signal.SIGTERM)
            stdout, stderr = process.communicate(timeout=10)
            if process.returncode != 0:
                raise AssertionError(f"server exit={process.returncode} stdout={stdout} stderr={stderr}")
            redis("DEL", "smt:datastream:heartbeat:AOI-VT-01")
            redis("DEL", "smt:datastream:auth:req:AOI-VT-01:E2E_VALID_REQUEST_0001")
            redis("DEL", "smt:datastream:auth:req:AOI-VT-01:E2E_INVALID_BODY_0001")
            mysql("DELETE FROM smt_datastream.device WHERE device_id='AOI-DISABLED-TEST'")

        log_text = (temporary_path / "datastream.log").read_text(encoding="utf-8")
        assert valid_headers["X-Signature"] not in log_text
        assert "smt-dev-aoi-vt-01" not in log_text
        assert environment["SMT_DATASTREAM_OPERATOR_TOKEN"] not in log_text
        assert body.decode() not in log_text


if __name__ == "__main__":
    main()
