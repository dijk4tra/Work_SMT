#!/usr/bin/env python3

import json
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path


def allocate_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def wait_for_port(port, process, timeout_seconds):
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


def request_json(url):
    try:
        with urllib.request.urlopen(url, timeout=3) as response:
            return response.status, json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        return error.code, json.loads(error.read().decode("utf-8"))


def stop_process(process):
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def main():
    if len(sys.argv) != 4:
        raise SystemExit(
            "usage: health_e2e_test.py <search-binary> <gateway-binary> <config>"
        )

    search_binary = Path(sys.argv[1]).resolve()
    gateway_binary = Path(sys.argv[2]).resolve()
    base_config_path = Path(sys.argv[3]).resolve()
    base_config = json.loads(base_config_path.read_text(encoding="utf-8"))

    with tempfile.TemporaryDirectory(prefix="logtrace-health-e2e-") as temporary:
        root = Path(temporary)
        archive_root = root / "archive"
        archive_root.mkdir()
        search_port = allocate_port()
        gateway_port = allocate_port()

        config = base_config
        config["search_rpc"]["port"] = search_port
        config["gateway"]["port"] = gateway_port
        config["gateway"]["rpc_port"] = search_port
        config["storage"]["archive_root"] = str(archive_root)
        config["storage"]["index_root"] = str(root / "index")
        config["indexing"]["poll_interval_ms"] = 60000
        config["logging"]["search_file"] = str(root / "search.log")
        config["logging"]["gateway_file"] = str(root / "gateway.log")
        config_path = root / "logtrace.json"
        config_path.write_text(json.dumps(config, indent=2), encoding="utf-8")

        search_log = (root / "search-process.log").open("wb")
        gateway_log = (root / "gateway-process.log").open("wb")
        search = subprocess.Popen(
            [str(search_binary), "--config", str(config_path)],
            stdout=search_log,
            stderr=subprocess.STDOUT,
        )
        gateway = None
        try:
            wait_for_port(search_port, search, 8)
            gateway = subprocess.Popen(
                [str(gateway_binary), "--config", str(config_path)],
                stdout=gateway_log,
                stderr=subprocess.STDOUT,
            )
            wait_for_port(gateway_port, gateway, 8)

            status, body = request_json(f"http://127.0.0.1:{gateway_port}/health/live")
            assert status == 200 and body["code"] == "OK"
            assert body["data"]["status"] == "alive"

            status, body = request_json(f"http://127.0.0.1:{gateway_port}/health/ready")
            assert status == 200 and body["code"] == "OK"
            assert body["data"]["status"] == "ready"

            stop_process(search)
            status, body = request_json(f"http://127.0.0.1:{gateway_port}/health/ready")
            assert status in (502, 504)
            assert body["code"] in ("SEARCH_RPC_UNAVAILABLE", "SEARCH_RPC_TIMEOUT")
        finally:
            if gateway is not None:
                stop_process(gateway)
            stop_process(search)
            search_log.close()
            gateway_log.close()


if __name__ == "__main__":
    main()
