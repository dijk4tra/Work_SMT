#!/usr/bin/env python3
"""在三线设备目录中并发模拟原子改名、稳定窗口和完成标志文件行为。"""

import argparse
import hashlib
import json
import os
import time
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
from pathlib import Path


DEVICES = [
    (line, kind, f"{prefix}-{index:02d}")
    for index, line in enumerate(("LINE-01", "LINE-02", "LINE-03"), start=1)
    for kind, prefix in (("SPI", "SPI-ZM"), ("AOI", "AOI-VT"), ("FCT", "FCT-TRI"))
]


def sidecar(path, file_type, result, produced_at):
    """写入采集端使用的最小厂商元数据 sidecar。"""
    metadata = {
        "work_order": "WO-COLLECTOR-E2E",
        "product_sn": "SN-" + path.stem.upper(),
        "file_type": file_type,
        "result": result,
        "produced_at": produced_at,
    }
    Path(str(path) + ".meta.json").write_text(json.dumps(metadata), encoding="utf-8")


def produce(root, entry, delay_seconds, large_bytes):
    """按设备类型执行对应封口行为并返回期望记录。"""
    line, kind, device = entry
    directory = root / line / device
    directory.mkdir(parents=True, exist_ok=True)
    produced_at = datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")
    if kind == "SPI":
        target = directory / f"{device}_result.json"
        content = json.dumps({"device_id": device, "result": "PASS"}).encode()
        sidecar(target, "DETECTION_RESULT", "PASS", produced_at)
        temporary = Path(str(target) + ".tmp")
        temporary.write_bytes(content)
        os.replace(temporary, target)
        export = directory / f"{device}_export.zip"
        export_content = (device.encode() + b"-EXPORT-") * 1024
        sidecar(export, "DEVICE_EXPORT", None, produced_at)
        export_temporary = Path(str(export) + ".tmp")
        export_temporary.write_bytes(export_content)
        os.replace(export_temporary, export)
        duplicate_export = directory / f"{device}_duplicate_export.zip"
        sidecar(duplicate_export, "DEVICE_EXPORT", None, produced_at)
        duplicate_temporary = Path(str(duplicate_export) + ".tmp")
        duplicate_temporary.write_bytes(export_content)
        os.replace(duplicate_temporary, duplicate_export)
        return [
            {"path": str(target), "device_id": device, "expected": "ARCHIVED",
             "file_size": len(content), "sha256": hashlib.sha256(content).hexdigest()},
            {"path": str(export), "device_id": device, "expected": "ARCHIVED",
             "file_size": len(export_content),
             "sha256": hashlib.sha256(export_content).hexdigest()},
            {"path": str(duplicate_export), "device_id": device, "expected": "ARCHIVED",
             "file_size": len(export_content),
             "sha256": hashlib.sha256(export_content).hexdigest()},
        ]
    elif kind == "AOI":
        target = directory / f"{device}_ng.png"
        content = (device.encode() + b"-AOI-NG-") * 4096
        sidecar(target, "NG_IMAGE", "NG", produced_at)
        with target.open("wb") as output:
            for offset in range(0, len(content), 4096):
                output.write(content[offset:offset + 4096])
                output.flush()
                os.fsync(output.fileno())
                time.sleep(delay_seconds)
    else:
        target = directory / f"{device}_report.csv"
        content = b"item,value,result\nvoltage,5.01,PASS\n"
        if large_bytes:
            content += b"x" * large_bytes
        sidecar(target, "TEST_REPORT", "PASS", produced_at)
        with target.open("wb") as output:
            output.write(content[:10])
            output.flush()
            time.sleep(delay_seconds)
            output.write(content[10:])
        Path(str(target) + ".done").write_text("done\n", encoding="ascii")
        runtime_log = directory / f"{device}_runtime.log"
        log_content = (f"device={device} status=RUNNING\n" * 20).encode()
        sidecar(runtime_log, "RUNTIME_LOG", None, produced_at)
        with runtime_log.open("wb") as output:
            output.write(log_content[:20])
            output.flush()
            time.sleep(delay_seconds)
            output.write(log_content[20:])
        Path(str(runtime_log) + ".done").write_text("done\n", encoding="ascii")
        return [
            {"path": str(target), "device_id": device, "expected": "ARCHIVED",
             "file_size": len(content), "sha256": hashlib.sha256(content).hexdigest()},
            {"path": str(runtime_log), "device_id": device, "expected": "ARCHIVED",
             "file_size": len(log_content),
             "sha256": hashlib.sha256(log_content).hexdigest()},
        ]
    return [{
        "path": str(target),
        "device_id": device,
        "expected": "ARCHIVED",
        "file_size": len(content),
        "sha256": hashlib.sha256(content).hexdigest(),
    }]


def main():
    """生成三线并发文件行为和独立期望清单。"""
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--delay-seconds", type=float, default=0.02)
    parser.add_argument("--large-file-bytes", type=int, default=0)
    parser.add_argument("--include-anomalies", action="store_true")
    parser.add_argument("--overwrite-existing", action="store_true")
    args = parser.parse_args()
    args.root.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(max_workers=9) as executor:
        groups = list(executor.map(
            lambda entry: produce(args.root, entry, args.delay_seconds,
                                  args.large_file_bytes if entry[2] == "FCT-TRI-03" else 0),
            DEVICES,
        ))
    records = [record for group in groups for record in group]
    if args.include_anomalies:
        directory = args.root / "LINE-01" / "SPI-ZM-01"
        zero = directory / "zero.json"
        zero.write_bytes(b"")
        sidecar(zero, "DETECTION_RESULT", "PASS",
                datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z"))
        records.append({"path": str(zero), "device_id": "SPI-ZM-01", "expected": "FAILED",
                        "file_size": 0, "sha256": hashlib.sha256(b"").hexdigest()})
        malformed = directory / "malformed.json"
        malformed.write_bytes(b"{broken")
        Path(str(malformed) + ".meta.json").write_text("{not-json", encoding="utf-8")
        records.append({"path": str(malformed), "device_id": "SPI-ZM-01",
                        "expected": "FAILED", "file_size": malformed.stat().st_size,
                        "sha256": hashlib.sha256(malformed.read_bytes()).hexdigest()})
        abandoned = directory / "abandoned.json.tmp"
        abandoned.write_bytes(b"partial")
        records.append({"path": str(abandoned), "device_id": "SPI-ZM-01",
                        "expected": "IGNORED", "file_size": abandoned.stat().st_size,
                        "sha256": hashlib.sha256(abandoned.read_bytes()).hexdigest()})
    if args.overwrite_existing:
        overwritten = args.root / "LINE-01" / "AOI-VT-01" / "AOI-VT-01_ng.png"
        content = b"overwritten-after-close"
        overwritten.write_bytes(content)
        for record in records:
            if record["path"] == str(overwritten):
                record["file_size"] = len(content)
                record["sha256"] = hashlib.sha256(content).hexdigest()
    manifest = {"generated_at": time.time(), "files": records}
    (args.root / "expected_manifest.json").write_text(
        json.dumps(manifest, indent=2), encoding="utf-8"
    )


if __name__ == "__main__":
    main()
