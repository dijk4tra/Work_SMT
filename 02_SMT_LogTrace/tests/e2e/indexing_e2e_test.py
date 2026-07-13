#!/usr/bin/env python3

import json
import os
import socket
import subprocess
import sys
import tempfile
import time
import uuid
from pathlib import Path


def run(command, env=None, input_text=None):
    return subprocess.run(
        command,
        check=True,
        env=env,
        input=input_text,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


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


def query(config, section, database, sql):
    password_name = config[section]["password_env"]
    result = run(
        mysql_command(config, section, database) + [f"--execute={sql}"],
        env=mysql_env(password_name),
    )
    return [line for line in result.stdout.splitlines() if line]


def main():
    if len(sys.argv) != 5:
        raise SystemExit(
            "usage: indexing_e2e_test.py <admin-binary> <search-binary> <base-config> <generator>"
        )

    admin_binary = Path(sys.argv[1]).resolve()
    search_binary = Path(sys.argv[2]).resolve()
    base_config_path = Path(sys.argv[3]).resolve()
    generator = Path(sys.argv[4]).resolve()
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
        config["indexing"]["poll_interval_ms"] = 100
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

            second = run(
                [str(admin_binary), "--config", str(config_path), "scan-once"], env=env
            )
            assert json.loads(second.stdout.splitlines()[-1])["batch_created"] is False

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
                    if batch_state == ["PARSED"]:
                        break
                    time.sleep(0.05)
                else:
                    raise RuntimeError("background index batch did not complete")
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
            assert batches == ["FAILED\tREBUILD_REQUESTED", "PARSED\t"]
            assert not (root / "index" / "parsed" / "batch_1").exists()
            assert (root / "index" / "parsed" / "batch_2" / "manifest.json").is_file()
        finally:
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
