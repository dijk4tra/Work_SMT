#!/usr/bin/env python3
"""验证三线设备目录到正式归档的采集、断网和重启恢复闭环。"""

import hashlib
import json
import os
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from upload_e2e_test import free_port, mysql_execute, redis, wait_ready


DEVICE_ROWS = [
    (line, f"ST-{kind}-{index:02d}", f"{prefix}-{index:02d}", f"IPC-L{index:02d}-01", mode)
    for index, line in enumerate(("LINE-01", "LINE-02", "LINE-03"), start=1)
    for kind, prefix, mode in (
        ("SPI", "SPI-ZM", "ATOMIC_RENAME"),
        ("AOI", "AOI-VT", "STABLE_WINDOW"),
        ("FCT", "FCT-TRI", "DONE_MARKER"),
    )
]


def collector_config(root, port):
    """构造测试限定的九设备采集配置。"""
    return {
        "server_url": f"http://127.0.0.1:{port}",
        "spool_root": str(root / "spool"),
        "scan_interval_ms": 100,
        "stable_scan_count": 3,
        "request_timeout_ms": 300,
        "chunk_size_bytes": 1048576,
        "spool_max_bytes": 209715200,
        "spool_min_free_bytes": 0,
        "retry": {"max_backoff_steps": 4, "base_delay_ms": 50, "max_delay_ms": 400},
        "devices": [
            {
                "line_id": line,
                "station_id": station,
                "device_id": device,
                "collector_id": collector,
                "input_dir": str(root / "inbox" / line / device),
                "secret_env": "SECRET_" + device.replace("-", "_"),
                "seal_mode": mode,
            }
            for line, station, device, collector, mode in DEVICE_ROWS
        ],
    }


def stop_process(process, timeout=10):
    """使用 SIGTERM 停止进程并核对正常退出。"""
    process.send_signal(signal.SIGTERM)
    stdout, stderr = process.communicate(timeout=timeout)
    if process.returncode != 0:
        raise AssertionError(
            f"process exit={process.returncode} stdout={stdout} stderr={stderr}"
        )


def load_states(root):
    """读取 collector 自己的持久状态，仅用于 E2E 断言。"""
    states = []
    for path in (root / "spool" / "states").glob("*.json"):
        states.append(json.loads(path.read_text(encoding="utf-8")))
    return states


def wait_for(predicate, timeout, message):
    """等待跨进程可观察条件成立。"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.02)
    raise AssertionError(message)


def main():
    """运行断网排队、退出、会话丢失、重启和最终归档核对。"""
    server = Path(sys.argv[1]).resolve()
    collector = Path(sys.argv[2]).resolve()
    simulator = Path(sys.argv[3]).resolve()
    source_config = Path(sys.argv[4]).resolve()
    environment = os.environ.copy()
    environment.setdefault("SMT_DATASTREAM_MYSQL_PASSWORD", "123456")
    environment.setdefault("SMT_DATASTREAM_OPERATOR_TOKEN", "e2e-operator-token-2026")
    for _, _, device, _, _ in DEVICE_ROWS:
        environment["SECRET_" + device.replace("-", "_")] = "smt-dev-" + device.lower()

    with tempfile.TemporaryDirectory(prefix="datastream-collector-e2e-") as temporary:
        root = Path(temporary)
        port = free_port()
        config = collector_config(root, port)
        for device in config["devices"]:
            Path(device["input_dir"]).mkdir(parents=True, exist_ok=True)
        collector_path = root / "collector.json"
        collector_path.write_text(json.dumps(config), encoding="utf-8")
        simulator_process = subprocess.Popen(
            [sys.executable, str(simulator), "--root", str(root / "inbox"),
             "--delay-seconds", "0.01", "--large-file-bytes", "8388608",
             "--include-anomalies"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        offline = subprocess.Popen(
            [str(collector), "--config", str(collector_path)],
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        simulator_stdout, simulator_stderr = simulator_process.communicate(timeout=20)
        if simulator_process.returncode != 0:
            raise AssertionError(
                f"simulator exit={simulator_process.returncode} "
                f"stdout={simulator_stdout} stderr={simulator_stderr}"
            )
        manifest = json.loads(
            (root / "inbox" / "expected_manifest.json").read_text(encoding="utf-8")
        )
        expected_archived = sum(
            item["expected"] == "ARCHIVED" for item in manifest["files"]
        )
        expected_failed = sum(item["expected"] == "FAILED" for item in manifest["files"])
        expected_tasks = expected_archived + expected_failed
        wait_for(
            lambda: len(load_states(root)) == expected_tasks,
            10,
            "collector did not persist all offline tasks",
        )
        time.sleep(0.5)
        stop_process(offline)
        offline_states = load_states(root)
        assert any(item["last_error"] == "NETWORK_ERROR" for item in offline_states)
        assert sum(item["state"] == "FAILED" for item in offline_states) == expected_failed

        server_config = json.loads(source_config.read_text(encoding="utf-8"))
        server_config["http"]["port"] = port
        server_config["redis"]["key_prefix"] = f"smt:test:collector:{os.getpid()}:"
        server_config["upload"]["temp_root"] = str(root / "server-upload")
        server_config["upload"]["archive_root"] = str(root / "server-archive")
        server_config["upload"]["min_free_space_bytes"] = 0
        server_config["upload"]["min_free_space_percent"] = 0
        server_config["logging"]["file"] = str(root / "server.log")
        server_path = root / "server.json"
        server_path.write_text(json.dumps(server_config), encoding="utf-8")
        server_process = subprocess.Popen(
            [str(server), "--config", str(server_path)],
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        collector_process = None
        try:
            wait_ready(port, server_process)
            collector_process = subprocess.Popen(
                [str(collector), "--config", str(collector_path)],
                env=environment,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            uploading_states = wait_for(
                lambda: (lambda states: states if
                         sum(item["state"] == "UPLOADING" for item in states) ==
                         expected_archived else None)(load_states(root)),
                10,
                "collector did not persist all recoverable upload sessions",
            )
            uploading = uploading_states[0]
            old_upload_id = uploading["upload_id"]
            collector_process.kill()
            collector_process.communicate(timeout=10)
            collector_process = None
            prefix = server_config["redis"]["key_prefix"] + "upload:" + old_upload_id
            redis("DEL", prefix, prefix + ":chunks", prefix + ":digests")

            collector_process = subprocess.Popen(
                [str(collector), "--config", str(collector_path)],
                env=environment,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            final_states = wait_for(
                lambda: (lambda states: states if
                         sum(item["state"] == "DONE" for item in states) ==
                         expected_archived else None)(
                             load_states(root)),
                40,
                "collector did not finish all valid files after restart",
            )
            stop_process(collector_process)
            collector_process = None
            recovered = next(item for item in final_states if item["task_id"] == uploading["task_id"])
            assert recovered["upload_id"] != old_upload_id
            assert sum(item["state"] == "FAILED" for item in final_states) == expected_failed

            mysql_environment = environment.copy()
            mysql_environment["MYSQL_PWD"] = environment["SMT_DATASTREAM_MYSQL_PASSWORD"]
            rows = subprocess.run(
                ["mysql", "-h", "127.0.0.1", "-u", "root", "smt_datastream", "-N", "-e",
                 "SELECT original_filename,file_size,LOWER(HEX(file_sha256)),relative_path "
                 "FROM archive_file WHERE work_order='WO-COLLECTOR-E2E'"],
                check=True,
                capture_output=True,
                text=True,
                env=mysql_environment,
            ).stdout.strip().splitlines()
            assert len(rows) == expected_archived
            archived = {row.split("\t")[0]: row.split("\t") for row in rows}
            for expected in manifest["files"]:
                if expected["expected"] != "ARCHIVED":
                    continue
                name = Path(expected["path"]).name
                row = archived[name]
                assert int(row[1]) == expected["file_size"]
                assert row[2] == expected["sha256"]
                final_path = root / "server-archive" / row[3]
                assert hashlib.sha256(final_path.read_bytes()).hexdigest() == expected["sha256"]
        finally:
            if collector_process is not None:
                stop_process(collector_process)
            stop_process(server_process)
            mysql_execute(
                "DELETE FROM archive_file WHERE work_order='WO-COLLECTOR-E2E'", environment
            )
            prefix = server_config["redis"]["key_prefix"]
            remaining = redis("--scan", "--pattern", prefix + "*")
            for key in remaining.splitlines():
                if key:
                    redis("DEL", key)


if __name__ == "__main__":
    main()
