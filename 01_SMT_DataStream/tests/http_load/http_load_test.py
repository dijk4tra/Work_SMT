#!/usr/bin/env python3
"""并发验证设备心跳与 Operator 查询的延迟和错误率。"""

import argparse
import hashlib
import hmac
import http.client
import json
import statistics
import threading
import time
import uuid
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from urllib.parse import urlparse


def percentile(values, percent):
    """返回 nearest-rank 百分位毫秒值。"""
    ordered = sorted(values)
    rank = max(0, (len(ordered) * percent + 99) // 100 - 1)
    return ordered[rank]


def request(target, method, path, body, headers):
    """发送一次 HTTP 请求并返回状态与耗时。"""
    started = time.perf_counter()
    connection = http.client.HTTPConnection(target.hostname, target.port, timeout=5)
    connection.request(method, path, body=body, headers=headers)
    response = connection.getresponse()
    response.read()
    connection.close()
    return response.status, (time.perf_counter() - started) * 1000


def heartbeat_headers(device_id, secret_text, path, body):
    """生成单次心跳请求使用的 HMAC 头。"""
    timestamp = str(int(time.time()))
    request_id = "load-" + uuid.uuid4().hex
    digest = hashlib.sha256(body).hexdigest()
    canonical = "\n".join(
        ["v1", "POST", path, device_id, timestamp, request_id, digest]
    )
    secret = hashlib.sha256(secret_text.encode()).digest()
    signature = hmac.new(secret, canonical.encode(), hashlib.sha256).hexdigest()
    return {
        "Content-Type": "application/json",
        "X-Device-Id": device_id,
        "X-Timestamp": timestamp,
        "X-Request-Id": request_id,
        "X-Content-SHA256": digest,
        "X-Signature": signature,
    }


def main():
    """执行固定请求总量，错误或 5xx 超过阈值时失败。"""
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default="http://127.0.0.1:8080")
    parser.add_argument("--scenario", choices=("heartbeat", "query"), required=True)
    parser.add_argument("--requests", type=int, default=300)
    parser.add_argument("--concurrency", type=int, default=12)
    parser.add_argument("--rate-per-second", type=float)
    parser.add_argument("--operator-token")
    parser.add_argument("--device-id", default="AOI-VT-01")
    parser.add_argument("--device-secret", default="smt-dev-aoi-vt-01")
    args = parser.parse_args()
    if args.requests < 1 or args.concurrency < 1:
        parser.error("requests and concurrency must be positive")
    if args.rate_per_second is not None and args.rate_per_second <= 0:
        parser.error("rate-per-second must be positive")
    if args.scenario == "query" and not args.operator_token:
        parser.error("--operator-token is required for query")

    target = urlparse(args.url)
    if target.scheme != "http" or not target.hostname or not target.port:
        parser.error("--url must be an explicit http URL with port")
    lock = threading.Lock()
    statuses = []
    latencies = []

    test_started = time.perf_counter()

    def execute(index):
        if args.rate_per_second is not None:
            scheduled = test_started + index / args.rate_per_second
            remaining = scheduled - time.perf_counter()
            if remaining > 0:
                time.sleep(remaining)
        if args.scenario == "heartbeat":
            path = "/api/v1/devices/heartbeat"
            body = json.dumps(
                {
                    "collector_id": "IPC-L01-01",
                    "software_version": "load-1.0",
                    "runtime_status": "RUNNING",
                    "work_order": "WO-LOAD-TEST",
                    "reported_at": time.strftime(
                        "%Y-%m-%dT%H:%M:00.000Z", time.gmtime()
                    ),
                },
                separators=(",", ":"),
            ).encode()
            headers = heartbeat_headers(args.device_id, args.device_secret, path, body)
            result = request(target, "POST", path, body, headers)
        else:
            result = request(
                target,
                "GET",
                "/api/v1/archives?work_order=WO-LOAD-TEST&page_size=50",
                b"",
                {"Authorization": "Bearer " + args.operator_token},
            )
        with lock:
            statuses.append(result[0])
            latencies.append(result[1])

    started = time.perf_counter()
    with ThreadPoolExecutor(max_workers=args.concurrency) as executor:
        list(executor.map(execute, range(args.requests)))
    duration = time.perf_counter() - started
    failures = sum(status < 200 or status >= 300 for status in statuses)
    server_errors = sum(status >= 500 for status in statuses)
    print(json.dumps({
        "scenario": args.scenario,
        "requests": len(statuses),
        "concurrency": args.concurrency,
        "configured_rate_per_second": args.rate_per_second,
        "duration_seconds": round(duration, 3),
        "requests_per_second": round(len(statuses) / duration, 2),
        "latency_ms_mean": round(statistics.mean(latencies), 2),
        "latency_ms_p50": round(percentile(latencies, 50), 2),
        "latency_ms_p95": round(percentile(latencies, 95), 2),
        "failures": failures,
        "server_errors": server_errors,
        "status_counts": dict(sorted(Counter(statuses).items())),
    }, sort_keys=True))
    if failures != 0 or server_errors != 0:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
