#!/usr/bin/env python3

import json
import os
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request
import uuid
from pathlib import Path


def run(command, env=None, input_text=None):
    result = subprocess.run(
        command,
        check=False,
        env=env,
        input=input_text,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def mysql_env(password_name):
    password = os.environ.get(password_name)
    if not password:
        raise RuntimeError(f"environment variable {password_name} is required")
    env = os.environ.copy()
    env["MYSQL_PWD"] = password
    return env


def mysql_command(config, section, database=None):
    value = config[section]
    command = [
        "mysql",
        "--protocol=TCP",
        f"--host={value['host']}",
        f"--port={value['port']}",
        f"--user={value['user']}",
        "--batch",
        "--skip-column-names",
        "--default-character-set=utf8mb4",
    ]
    if database:
        command.append(f"--database={database}")
    return command


def allocate_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def stop_process(process):
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def wait_for_port(port, process, timeout_seconds=8):
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"process exited early with code {process.returncode}")
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError(f"port {port} did not become ready")


class PingOnlyRedis:
    def __init__(self, port):
        self.port = port
        self.enabled = True
        self.listener = None
        self.thread = None

    def start(self):
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.listener.bind(("127.0.0.1", self.port))
        self.listener.listen()
        self.listener.settimeout(0.1)
        self.thread = threading.Thread(target=self._serve, daemon=True)
        self.thread.start()

    def _serve(self):
        while self.enabled:
            try:
                connection, _ = self.listener.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            with connection:
                connection.settimeout(0.5)
                try:
                    request = connection.recv(4096).upper()
                    if b"PING" in request:
                        connection.sendall(b"+PONG\r\n")
                except OSError:
                    pass

    def stop(self):
        self.enabled = False
        if self.listener is not None:
            self.listener.close()
        if self.thread is not None:
            self.thread.join(timeout=1)


def http_json(url, token=None, method="GET", body=None):
    headers = {}
    if token is not None:
        headers["Authorization"] = f"Bearer {token}"
    data = None
    if body is not None:
        headers["Content-Type"] = "application/json"
        data = json.dumps(body).encode("utf-8")
    request = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(request, timeout=5) as response:
            return response.status, json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        return error.code, json.loads(error.read().decode("utf-8"))


def query(config, section, database, sql):
    password_name = config[section]["password_env"]
    result = run(
        mysql_command(config, section, database) + [f"--execute={sql}"],
        env=mysql_env(password_name),
    )
    return [line for line in result.stdout.splitlines() if line]


def main():
    if len(sys.argv) != 6:
        raise SystemExit(
            "usage: indexing_e2e_test.py <admin-binary> <search-binary> "
            "<gateway-binary> <base-config> <generator>"
        )

    admin_binary = Path(sys.argv[1]).resolve()
    search_binary = Path(sys.argv[2]).resolve()
    gateway_binary = Path(sys.argv[3]).resolve()
    base_config_path = Path(sys.argv[4]).resolve()
    generator = Path(sys.argv[5]).resolve()
    project_root = base_config_path.parent.parent
    db_script = project_root / "scripts" / "db.sh"
    base_config = json.loads(base_config_path.read_text(encoding="utf-8"))

    with tempfile.TemporaryDirectory(prefix="logtrace-indexing-e2e-") as temporary:
        root = Path(temporary)
        suffix = uuid.uuid4().hex[:12]
        source_database = f"logtrace_e2e_source_{suffix}"
        state_database = f"logtrace_e2e_state_{suffix}"
        config = json.loads(json.dumps(base_config))
        config["source_mysql"]["database"] = source_database
        config["state_mysql"]["database"] = state_database
        config["storage"]["archive_root"] = str(root / "samples" / "archive")
        config["storage"]["index_root"] = str(root / "index")
        config["search_rpc"]["port"] = allocate_port()
        config["gateway"]["rpc_port"] = config["search_rpc"]["port"]
        config["gateway"]["port"] = allocate_port()
        config["indexing"]["poll_interval_ms"] = 100
        config["redis"]["key_prefix"] = f"smt:logtrace:e2e:{suffix}:"
        config["logging"]["search_file"] = str(root / "search.log")
        config["logging"]["gateway_file"] = str(root / "gateway.log")
        config_path = root / "logtrace.json"
        config_path.write_text(json.dumps(config, indent=2), encoding="utf-8")

        source_env = mysql_env(config["source_mysql"]["password_env"])
        state_env = mysql_env(config["state_mysql"]["password_env"])
        try:
            run(
                mysql_command(config, "source_mysql")
                + [
                    "--execute="
                    f"CREATE DATABASE `{source_database}` CHARACTER SET utf8mb4 "
                    "COLLATE utf8mb4_0900_ai_ci"
                ],
                env=source_env,
            )
            run(
                mysql_command(config, "state_mysql")
                + [
                    "--execute="
                    f"CREATE DATABASE `{state_database}` CHARACTER SET utf8mb4 "
                    "COLLATE utf8mb4_0900_ai_ci"
                ],
                env=state_env,
            )
            source_schema = """
CREATE TABLE archive_file (
    archive_id BIGINT UNSIGNED NOT NULL,
    line_id VARCHAR(64) NOT NULL,
    station_id VARCHAR(64) NOT NULL,
    device_id VARCHAR(64) NOT NULL,
    collector_id VARCHAR(64) NOT NULL,
    work_order VARCHAR(64) NULL,
    product_sn VARCHAR(96) NULL,
    file_type VARCHAR(32) NOT NULL,
    original_filename VARCHAR(255) NOT NULL,
    relative_path VARCHAR(512) NOT NULL,
    file_size BIGINT UNSIGNED NOT NULL,
    file_sha256 BINARY(32) NOT NULL,
    produced_at DATETIME(3) NOT NULL,
    archived_at DATETIME(3) NOT NULL,
    PRIMARY KEY (archive_id)
) ENGINE=InnoDB;
"""
            run(
                mysql_command(config, "source_mysql", source_database),
                env=source_env,
                input_text=source_schema,
            )
            env = os.environ.copy()
            env["SMT_LOGTRACE_OPERATOR_TOKEN"] = "indexing-e2e-token"
            run([str(db_script), "migrate", "--config", str(config_path)], env=env)
            run([str(db_script), "seed", "--config", str(config_path)], env=env)
            run([sys.executable, str(generator), "--output", str(root / "samples")])
            archive_sql = (root / "samples" / "archive_rows.sql").read_text(encoding="utf-8")
            run(
                mysql_command(config, "source_mysql", source_database),
                env=source_env,
                input_text=archive_sql,
            )

            first = run(
                [str(admin_binary), "--config", str(config_path), "scan-once"], env=env
            )
            summary = json.loads(first.stdout.splitlines()[-1])
            assert summary == {
                "batch_created": True,
                "batch_id": 1,
                "source_file_count": 5,
                "parsed_file_count": 3,
                "failed_file_count": 2,
                "document_count": 7,
            }
            states = query(
                config,
                "state_mysql",
                state_database,
                "SELECT state,COUNT(*) FROM indexed_archive GROUP BY state ORDER BY state",
            )
            assert states == ["FAILED\t2", "PARSED\t3"]
            failures = query(
                config,
                "state_mysql",
                state_database,
                "SELECT archive_id,failure_code FROM indexed_archive "
                "WHERE state='FAILED' ORDER BY archive_id",
            )
            assert failures == [
                "4\tPARSER_PROFILE_NOT_FOUND",
                "5\tARCHIVE_SHA256_MISMATCH",
            ]
            documents = root / "index" / "parsed" / "batch_1" / "documents.jsonl"
            assert len(documents.read_text(encoding="utf-8").splitlines()) == 7

            built = run(
                [str(admin_binary), "--config", str(config_path), "build-once"], env=env
            )
            build_summary = json.loads(built.stdout.splitlines()[-1])
            assert build_summary["batch_built"] is True
            assert build_summary["batch_id"] == 1
            assert build_summary["segment_name"] == "segment_1"
            assert build_summary["document_count"] == 7
            assert build_summary["term_count"] > 0
            assert build_summary["posting_count"] >= build_summary["term_count"]
            assert build_summary["snapshot_version"] == 1
            assert build_summary["snapshot_segment_count"] == 1
            assert build_summary["snapshot_document_count"] == 7
            states = query(
                config,
                "state_mysql",
                state_database,
                "SELECT state,COUNT(*) FROM indexed_archive GROUP BY state ORDER BY state",
            )
            assert states == ["FAILED\t2", "INDEXED\t3"]
            segment = root / "index" / "segments" / "segment_1"
            assert sorted(path.name for path in segment.iterdir()) == [
                "documents.bin",
                "files.bin",
                "manifest.json",
                "postings.bin",
                "terms.bin",
            ]

            api_search_log = (root / "api-search.log").open("wb")
            api_gateway_log = (root / "api-gateway.log").open("wb")
            api_search = subprocess.Popen(
                [str(search_binary), "--config", str(config_path)],
                env=env,
                stdout=api_search_log,
                stderr=subprocess.STDOUT,
            )
            api_gateway = None
            try:
                wait_for_port(config["search_rpc"]["port"], api_search)
                api_gateway = subprocess.Popen(
                    [str(gateway_binary), "--config", str(config_path)],
                    env=env,
                    stdout=api_gateway_log,
                    stderr=subprocess.STDOUT,
                )
                wait_for_port(config["gateway"]["port"], api_gateway)
                base_url = f"http://127.0.0.1:{config['gateway']['port']}"
                status, response = http_json(
                    f"{base_url}/api/v1/logs/search",
                    method="POST",
                    body={
                        "keywords": ["inspection", "ng"],
                        "device_id": "AOI-VT-01",
                        "offset": 0,
                        "page_size": 10,
                    },
                )
                assert status == 401 and response["code"] == "OPERATOR_TOKEN_INVALID"
                status, response = http_json(
                    f"{base_url}/api/v1/logs/search",
                    token="indexing-e2e-token",
                    method="POST",
                    body={
                        "keywords": ["inspection", "ng"],
                        "device_id": "AOI-VT-01",
                        "offset": 0,
                        "page_size": 10,
                    },
                )
                assert status == 200 and response["data"]["total_hits"] == 1
                first_search_data = response["data"]
                redis_command = [
                    "redis-cli",
                    "--raw",
                    "-h",
                    config["redis"]["host"],
                    "-p",
                    str(config["redis"]["port"]),
                    "--scan",
                    "--pattern",
                    f"{config['redis']['key_prefix']}query:v1:1:*",
                ]
                keys = run(redis_command).stdout.splitlines()
                assert len(keys) == 1
                status, response = http_json(
                    f"{base_url}/api/v1/logs/search",
                    token="indexing-e2e-token",
                    method="POST",
                    body={
                        "keywords": ["NG", "inspection"],
                        "device_id": "AOI-VT-01",
                        "offset": 0,
                        "page_size": 10,
                    },
                )
                assert status == 200 and response["data"] == first_search_data
                run(
                    [
                        "redis-cli",
                        "--raw",
                        "-h",
                        config["redis"]["host"],
                        "-p",
                        str(config["redis"]["port"]),
                        "SETEX",
                        keys[0],
                        "30",
                        "damaged-cache-value",
                    ]
                )
                status, response = http_json(
                    f"{base_url}/api/v1/logs/search",
                    token="indexing-e2e-token",
                    method="POST",
                    body={
                        "keywords": ["inspection", "ng"],
                        "device_id": "AOI-VT-01",
                        "offset": 0,
                        "page_size": 10,
                    },
                )
                assert status == 200 and response["data"] == first_search_data
                doc_id = response["data"]["items"][0]["doc_id"]
                assert response["data"]["items"][0]["error_code"] == "INSPECTION_NG"
                status, response = http_json(
                    f"{base_url}/api/v1/logs/anomalies?device_id=AOI-VT-01&offset=0&page_size=10",
                    token="indexing-e2e-token",
                )
                assert status == 200 and response["data"]["total_hits"] == 1
                status, response = http_json(
                    f"{base_url}/api/v1/logs/{doc_id}", token="indexing-e2e-token"
                )
                assert status == 200 and "code=INSPECTION_NG" in response["data"]["raw"]
                status, response = http_json(
                    f"{base_url}/api/v1/error-codes/INSPECTION_NG",
                    token="indexing-e2e-token",
                )
                assert status == 200 and response["data"]["module_name"] == "inspection"
                assert len(response["data"]["matching_logs"]) == 1
            finally:
                if api_gateway is not None:
                    stop_process(api_gateway)
                stop_process(api_search)
                api_search_log.close()
                api_gateway_log.close()

            fallback_config = json.loads(json.dumps(config))
            fallback_config["redis"]["port"] = allocate_port()
            fallback_config["search_rpc"]["port"] = allocate_port()
            fallback_config["gateway"]["rpc_port"] = fallback_config["search_rpc"]["port"]
            fallback_config["gateway"]["port"] = allocate_port()
            fallback_config_path = root / "fallback-logtrace.json"
            fallback_config_path.write_text(
                json.dumps(fallback_config, indent=2), encoding="utf-8"
            )
            fake_redis = PingOnlyRedis(fallback_config["redis"]["port"])
            fake_redis.start()
            fallback_search = subprocess.Popen(
                [str(search_binary), "--config", str(fallback_config_path)], env=env,
                stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT
            )
            fallback_gateway = None
            try:
                wait_for_port(fallback_config["search_rpc"]["port"], fallback_search)
                fallback_gateway = subprocess.Popen(
                    [str(gateway_binary), "--config", str(fallback_config_path)], env=env,
                    stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT
                )
                wait_for_port(fallback_config["gateway"]["port"], fallback_gateway)
                fake_redis.stop()
                status, response = http_json(
                    f"http://127.0.0.1:{fallback_config['gateway']['port']}"
                    "/api/v1/logs/search",
                    token="indexing-e2e-token",
                    method="POST",
                    body={"keywords": ["inspection", "ng"], "device_id": "AOI-VT-01",
                          "offset": 0, "page_size": 10},
                )
                assert status == 200 and response["data"]["total_hits"] == 1
            finally:
                if fallback_gateway is not None:
                    stop_process(fallback_gateway)
                stop_process(fallback_search)
                fake_redis.stop()

            second = run(
                [str(admin_binary), "--config", str(config_path), "scan-once"], env=env
            )
            assert json.loads(second.stdout.splitlines()[-1])["batch_created"] is False
            second_build = run(
                [str(admin_binary), "--config", str(config_path), "build-once"], env=env
            )
            second_build_summary = json.loads(second_build.stdout.splitlines()[-1])
            assert second_build_summary["batch_built"] is False
            assert second_build_summary["snapshot_version"] == 1

            query(
                config,
                "state_mysql",
                state_database,
                "UPDATE index_batch SET state='BUILDING',segment_sha256=NULL,"
                "published_at=NULL WHERE batch_id=1",
            )
            orphan = root / "index" / "segments" / "segment_999"
            orphan.mkdir()
            search_log = (root / "search-recovery.log").open("wb")
            search = subprocess.Popen(
                [str(search_binary), "--config", str(config_path)],
                env=env,
                stdout=search_log,
                stderr=subprocess.STDOUT,
            )
            try:
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    if search.poll() is not None:
                        raise RuntimeError(
                            f"search recovery exited early with code {search.returncode}"
                        )
                    recovered = query(
                        config,
                        "state_mysql",
                        state_database,
                        "SELECT state FROM index_batch WHERE batch_id=1",
                    )
                    if recovered == ["READY"]:
                        break
                    time.sleep(0.05)
                else:
                    raise RuntimeError("renamed BUILDING Segment was not recovered")
            finally:
                stop_process(search)
                search_log.close()
            assert search.returncode == 0
            assert orphan.is_dir()

            terms_path = segment / "terms.bin"
            original_terms = terms_path.read_bytes()
            terms_path.write_bytes(original_terms + b"x")
            corrupt_log = (root / "search-corrupt.log").open("wb")
            corrupt_search = subprocess.Popen(
                [str(search_binary), "--config", str(config_path)],
                env=env,
                stdout=corrupt_log,
                stderr=subprocess.STDOUT,
            )
            try:
                corrupt_search.wait(timeout=5)
            finally:
                stop_process(corrupt_search)
                corrupt_log.close()
                terms_path.write_bytes(original_terms)
            assert corrupt_search.returncode != 0

            run(
                [
                    str(admin_binary),
                    "--config",
                    str(config_path),
                    "rebuild",
                    "--archive-id",
                    "1",
                ],
                env=env,
            )
            assert not segment.exists()
            search_log = (root / "search-process.log").open("wb")
            search = subprocess.Popen(
                [str(search_binary), "--config", str(config_path)],
                env=env,
                stdout=search_log,
                stderr=subprocess.STDOUT,
            )
            try:
                deadline = time.monotonic() + 8
                while time.monotonic() < deadline:
                    if search.poll() is not None:
                        raise RuntimeError(
                            f"search process exited early with code {search.returncode}"
                        )
                    batch_state = query(
                        config,
                        "state_mysql",
                        state_database,
                        "SELECT state FROM index_batch WHERE batch_id=2",
                    )
                    if batch_state == ["READY"]:
                        break
                    time.sleep(0.05)
                else:
                    raise RuntimeError("background READY Segment did not complete")
            finally:
                stop_process(search)
                search_log.close()
            assert search.returncode == 0
            batches = query(
                config,
                "state_mysql",
                state_database,
                "SELECT state,COALESCE(failure_code,'') FROM index_batch ORDER BY batch_id",
            )
            assert batches == ["FAILED\tREBUILD_REQUESTED", "READY\t"]
            assert not (root / "index" / "parsed" / "batch_1").exists()
            assert (root / "index" / "parsed" / "batch_2" / "manifest.json").is_file()
            assert (root / "index" / "segments" / "segment_2" / "manifest.json").is_file()
            states = query(
                config,
                "state_mysql",
                state_database,
                "SELECT state,COUNT(*) FROM indexed_archive GROUP BY state ORDER BY state",
            )
            assert states == ["FAILED\t2", "INDEXED\t3"]

            temporary_build = root / "index" / "segments" / ".building" / "segment_777"
            temporary_build.mkdir()
            final_log = (root / "search-final-recovery.log").open("wb")
            final_search = subprocess.Popen(
                [str(search_binary), "--config", str(config_path)],
                env=env,
                stdout=final_log,
                stderr=subprocess.STDOUT,
            )
            try:
                time.sleep(0.3)
                if final_search.poll() is not None:
                    raise RuntimeError(
                        f"final snapshot recovery exited with code {final_search.returncode}"
                    )
            finally:
                stop_process(final_search)
                final_log.close()
            assert final_search.returncode == 0
            assert not temporary_build.exists()
        finally:
            redis_keys = run(
                ["redis-cli", "--raw", "-h", config["redis"]["host"], "-p",
                 str(config["redis"]["port"]), "--scan", "--pattern",
                 f"{config['redis']['key_prefix']}*"]
            ).stdout.splitlines()
            if redis_keys:
                run(["redis-cli", "--raw", "-h", config["redis"]["host"], "-p",
                     str(config["redis"]["port"]), "DEL"] + redis_keys)
            run(
                mysql_command(config, "source_mysql")
                + [f"--execute=DROP DATABASE IF EXISTS `{source_database}`"],
                env=source_env,
            )
            run(
                mysql_command(config, "state_mysql")
                + [f"--execute=DROP DATABASE IF EXISTS `{state_database}`"],
                env=state_env,
            )


if __name__ == "__main__":
    main()
