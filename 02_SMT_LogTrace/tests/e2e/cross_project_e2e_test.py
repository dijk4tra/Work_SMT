#!/usr/bin/env python3
"""验证 Collector、DataStream、LogTrace 和 HTTP 查询的真实跨项目闭环。"""

import json
import os
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request
import uuid
from datetime import datetime, timezone
from pathlib import Path


def run(command, environment=None):
    result = subprocess.run(command, check=False, env=environment, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        raise RuntimeError(f"command failed: {' '.join(command)}\n{result.stdout}\n{result.stderr}")
    return result


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def wait_port(port, process, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"process exited early: {process.returncode}")
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError(f"port {port} was not ready")


def stop(process):
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=8)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def mysql_command(section, database=None):
    command = ["mysql", "--protocol=TCP", f"--host={section['host']}",
               f"--port={section['port']}", f"--user={section['user']}", "--batch",
               "--skip-column-names", "--default-character-set=utf8mb4"]
    if database:
        command.append(f"--database={database}")
    return command


def mysql(config, section_name, sql, database=None):
    section = config[section_name]
    environment = os.environ.copy()
    environment["MYSQL_PWD"] = os.environ[section["password_env"]]
    return run(mysql_command(section, database) + [f"--execute={sql}"], environment).stdout


def http_search(port, token):
    body = json.dumps({"keywords": ["inspection", "ng"], "device_id": "AOI-VT-01",
                       "offset": 0, "page_size": 10}).encode("utf-8")
    request = urllib.request.Request(
        f"http://127.0.0.1:{port}/api/v1/logs/search", data=body, method="POST",
        headers={"Authorization": f"Bearer {token}", "Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=5) as response:
        return json.loads(response.read().decode("utf-8"))


def main():
    if len(sys.argv) != 10:
        raise SystemExit("usage: cross_project_e2e_test.py <datastream-server> <collector> "
                         "<datastream-config> <datastream-db-script> <logtrace-admin> "
                         "<logsearch-server> <gateway> <logtrace-config> <logtrace-db-script>")
    datastream_server, collector, datastream_config_path, datastream_db, logtrace_admin, \
        logsearch_server, gateway, logtrace_config_path, logtrace_db = map(Path, sys.argv[1:])
    environment = os.environ.copy()
    environment.setdefault("SMT_DATASTREAM_OPERATOR_TOKEN", "cross-e2e-datastream-token")
    environment.setdefault("SMT_LOGTRACE_OPERATOR_TOKEN", "cross-e2e-logtrace-token")
    required = ["SMT_LOGTRACE_SOURCE_MYSQL_PASSWORD", "SMT_LOGTRACE_STATE_MYSQL_PASSWORD"]
    for name in required:
        if not environment.get(name):
            raise RuntimeError(f"environment variable {name} is required")
    environment["SMT_SECRET_AOI_VT_01"] = "smt-dev-aoi-vt-01"

    with tempfile.TemporaryDirectory(prefix="smt-cross-e2e-") as temporary:
        root = Path(temporary)
        suffix = uuid.uuid4().hex[:12]
        source_database = f"smt_cross_source_{suffix}"
        state_database = f"smt_cross_state_{suffix}"
        datastream_config = json.loads(datastream_config_path.read_text(encoding="utf-8"))
        logtrace_config = json.loads(logtrace_config_path.read_text(encoding="utf-8"))
        datastream_config["mysql"]["database"] = source_database
        datastream_config["mysql"]["password_env"] = "SMT_LOGTRACE_SOURCE_MYSQL_PASSWORD"
        datastream_config["http"]["port"] = free_port()
        datastream_config["redis"]["key_prefix"] = f"smt:cross:{suffix}:datastream:"
        datastream_config["upload"]["temp_root"] = str(root / "upload")
        datastream_config["upload"]["archive_root"] = str(root / "archive")
        datastream_config["upload"]["min_free_space_bytes"] = 0
        datastream_config["upload"]["min_free_space_percent"] = 0
        datastream_config["logging"]["file"] = str(root / "datastream.log")
        datastream_path = root / "datastream.json"
        datastream_path.write_text(json.dumps(datastream_config), encoding="utf-8")

        logtrace_config["source_mysql"]["database"] = source_database
        logtrace_config["state_mysql"]["database"] = state_database
        logtrace_config["storage"]["archive_root"] = str(root / "archive")
        logtrace_config["storage"]["index_root"] = str(root / "index")
        logtrace_config["search_rpc"]["port"] = free_port()
        logtrace_config["gateway"]["rpc_port"] = logtrace_config["search_rpc"]["port"]
        logtrace_config["gateway"]["port"] = free_port()
        logtrace_config["redis"]["key_prefix"] = f"smt:cross:{suffix}:logtrace:"
        logtrace_config["logging"]["search_file"] = str(root / "search.log")
        logtrace_config["logging"]["gateway_file"] = str(root / "gateway.log")
        logtrace_path = root / "logtrace.json"
        logtrace_path.write_text(json.dumps(logtrace_config), encoding="utf-8")

        mysql({"mysql": datastream_config["mysql"]}, "mysql",
              f"CREATE DATABASE `{source_database}` CHARACTER SET utf8mb4 "
              "COLLATE utf8mb4_0900_ai_ci")
        mysql(logtrace_config, "state_mysql", f"CREATE DATABASE `{state_database}` "
              "CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci")
        datastream = collector_process = search = gateway_process = None
        try:
            run([str(datastream_db), "migrate", "--config", str(datastream_path)], environment)
            run([str(datastream_db), "seed", "--config", str(datastream_path)], environment)
            run([str(logtrace_db), "migrate", "--config", str(logtrace_path)], environment)
            run([str(logtrace_db), "seed", "--config", str(logtrace_path)], environment)

            inbox = root / "inbox"
            inbox.mkdir()
            collector_config = {
                "server_url": f"http://127.0.0.1:{datastream_config['http']['port']}",
                "spool_root": str(root / "spool"), "scan_interval_ms": 100,
                "stable_scan_count": 2, "request_timeout_ms": 1000,
                "chunk_size_bytes": 1048576, "spool_max_bytes": 104857600,
                "spool_min_free_bytes": 0,
                "retry": {"max_backoff_steps": 3, "base_delay_ms": 50, "max_delay_ms": 200},
                "devices": [{"line_id": "LINE-01", "station_id": "ST-AOI-01",
                             "device_id": "AOI-VT-01", "collector_id": "IPC-L01-01",
                             "input_dir": str(inbox), "secret_env": "SMT_SECRET_AOI_VT_01",
                             "seal_mode": "STABLE_WINDOW"}]}
            collector_path = root / "collector.json"
            collector_path.write_text(json.dumps(collector_config), encoding="utf-8")
            datastream = subprocess.Popen([str(datastream_server), "--config", str(datastream_path)],
                                          env=environment, stdout=subprocess.DEVNULL,
                                          stderr=subprocess.STDOUT)
            wait_port(datastream_config["http"]["port"], datastream)
            collector_process = subprocess.Popen([str(collector), "--config", str(collector_path)],
                                                 env=environment, stdout=subprocess.DEVNULL,
                                                 stderr=subprocess.STDOUT)
            produced_at = datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace(
                "+00:00", "Z")
            log = inbox / "cross_runtime.log"
            Path(str(log) + ".meta.json").write_text(json.dumps({
                "work_order": "WO-CROSS-E2E", "product_sn": "SN-CROSS-0001",
                "file_type": "RUNTIME_LOG", "result": None, "produced_at": produced_at}),
                encoding="utf-8")
            log.write_text(
                "2026-07-14T08:00:00.000+08:00 level=ERROR module=inspection "
                "device=AOI-VT-01 station=ST-AOI-01 sn=SN-CROSS-0001 "
                "code=INSPECTION_NG result=NG\n", encoding="utf-8")
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                count = mysql({"mysql": datastream_config["mysql"]}, "mysql",
                              "SELECT COUNT(*) FROM archive_file WHERE work_order='WO-CROSS-E2E'",
                              source_database).strip()
                if count == "1":
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError("Collector did not create the DataStream archive")
            stop(collector_process)
            collector_process = None
            stop(datastream)
            datastream = None

            scan = json.loads(run([str(logtrace_admin), "--config", str(logtrace_path),
                                   "scan-once"], environment).stdout.splitlines()[-1])
            if scan["parsed_file_count"] != 1 or scan["document_count"] != 1:
                raise RuntimeError(f"unexpected scan result: {scan}")
            build = json.loads(run([str(logtrace_admin), "--config", str(logtrace_path),
                                    "build-once"], environment).stdout.splitlines()[-1])
            if not build["batch_built"]:
                raise RuntimeError(f"unexpected build result: {build}")
            search = subprocess.Popen([str(logsearch_server), "--config", str(logtrace_path)],
                                      env=environment, stdout=subprocess.DEVNULL,
                                      stderr=subprocess.STDOUT)
            wait_port(logtrace_config["search_rpc"]["port"], search)
            gateway_process = subprocess.Popen([str(gateway), "--config", str(logtrace_path)],
                                               env=environment, stdout=subprocess.DEVNULL,
                                               stderr=subprocess.STDOUT)
            wait_port(logtrace_config["gateway"]["port"], gateway_process)
            response = http_search(logtrace_config["gateway"]["port"],
                                   environment["SMT_LOGTRACE_OPERATOR_TOKEN"])
            data = response["data"]
            if data["total_hits"] != 1 or data["items"][0]["error_code"] != "INSPECTION_NG":
                raise RuntimeError(f"unexpected HTTP search result: {response}")
            print(json.dumps({"archive_count": 1, "document_count": 1,
                              "snapshot_version": data["snapshot_version"],
                              "http_hits": data["total_hits"]}))
        finally:
            stop(gateway_process)
            stop(search)
            stop(collector_process)
            stop(datastream)
            mysql({"mysql": datastream_config["mysql"]}, "mysql",
                  f"DROP DATABASE IF EXISTS `{source_database}`")
            mysql(logtrace_config, "state_mysql", f"DROP DATABASE IF EXISTS `{state_database}`")


if __name__ == "__main__":
    main()
