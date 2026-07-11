#!/usr/bin/env python3
"""生成 SMT 检测工位、测试台和工控机的模拟原始业务文件。"""

import argparse
import binascii
import csv
import hashlib
import io
import json
import random
import struct
import time
import zipfile
import zlib
from datetime import datetime, timedelta, timezone
from pathlib import Path


DEVICE_TYPES = (
    ("SPI", "SPI-ZM", "Zenith 2 Alpha", "3.8.12"),
    ("AOI", "AOI-VT", "VT-S730", "5.4.7"),
    ("FCT", "FCT-TRI", "TR5001 FCT", "2.6.3"),
)
DEVICES = tuple(
    {
        "line_id": f"LINE-{line:02d}",
        "station_id": f"ST-{station_type}-{line:02d}",
        "station_type": station_type,
        "device_id": f"{device_prefix}-{line:02d}",
        "device_model": device_model,
        "collector_id": f"IPC-L{line:02d}-01",
        "software_version": software_version,
    }
    for line in range(1, 4)
    for station_type, device_prefix, device_model, software_version in DEVICE_TYPES
)

PRODUCT_MODELS = ("CTRL-MB-A1", "POWER-DRV-B2")
DEFECTS = (
    ("SOLDER_BRIDGE", "焊锡桥连"),
    ("INSUFFICIENT_SOLDER", "少锡"),
    ("COMPONENT_MISSING", "缺件"),
    ("POLARITY_REVERSED", "极性反"),
    ("OPEN_CIRCUIT", "开路"),
)


def parse_args():
    parser = argparse.ArgumentParser(description="生成可持续追加的 SMT 原始业务数据")
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "data" / "mock_inbox",
        help="模拟数据根目录",
    )
    parser.add_argument("--cycles", type=int, default=12, help="批量模式生成周期数")
    parser.add_argument("--step-seconds", type=int, default=30, help="模拟业务时间步长")
    parser.add_argument("--seed", type=int, default=20260711, help="随机种子")
    parser.add_argument("--start-time", help="ISO 8601 起始时间，默认使用当前本地时间")
    parser.add_argument("--continuous", action="store_true", help="持续生成，按 Ctrl+C 结束")
    parser.add_argument("--interval-seconds", type=float, default=5.0, help="持续模式真实等待间隔")
    args = parser.parse_args()
    if args.cycles <= 0:
        parser.error("--cycles 必须大于 0")
    if args.step_seconds <= 0:
        parser.error("--step-seconds 必须大于 0")
    if args.interval_seconds <= 0:
        parser.error("--interval-seconds 必须大于 0")
    return args


def parse_start_time(value):
    if value is None:
        return datetime.now().astimezone().replace(microsecond=0)
    parsed = datetime.fromisoformat(value)
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=datetime.now().astimezone().tzinfo)
    return parsed


def compact_time(value):
    return value.strftime("%Y%m%dT%H%M%S%z")


def iso_time(value):
    return value.isoformat(timespec="milliseconds")


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_json(path, payload):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as target:
        json.dump(payload, target, ensure_ascii=False, indent=2)
        target.write("\n")


def png_chunk(kind, payload):
    crc = binascii.crc32(kind + payload) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", crc)


def write_ng_png(path, rng, defect_code, reference):
    width, height = 640, 360
    defect_x = rng.randint(120, 500)
    defect_y = rng.randint(80, 260)
    rows = []
    for y in range(height):
        row = bytearray([0])
        for x in range(width):
            grid = (x % 40 < 2) or (y % 40 < 2)
            red_box = abs(x - defect_x) < 42 and abs(y - defect_y) < 34
            border = red_box and (abs(x - defect_x) > 38 or abs(y - defect_y) > 30)
            if border:
                row.extend((235, 35, 35))
            elif grid:
                row.extend((53, 95, 70))
            else:
                noise = rng.randint(0, 16)
                row.extend((24 + noise, 62 + noise, 40 + noise // 2))
        rows.append(bytes(row))
    metadata = f"defect={defect_code};reference={reference}".encode("latin-1")
    png = b"\x89PNG\r\n\x1a\n"
    png += png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += png_chunk(b"tEXt", b"Description\x00" + metadata)
    png += png_chunk(b"IDAT", zlib.compress(b"".join(rows), level=6))
    png += png_chunk(b"IEND", b"")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def base_context(device, moment, sequence):
    product_model = PRODUCT_MODELS[(sequence // 8) % len(PRODUCT_MODELS)]
    serial = f"{product_model.replace('-', '')}-{moment:%y%m%d}-{sequence:06d}"
    return {
        **device,
        "work_order": f"WO-{moment:%Y%m%d}-{1 + sequence // 80:03d}",
        "product_model": product_model,
        "product_sn": serial,
        "panel_sn": f"PNL-{moment:%y%m%d}-{sequence // 4:05d}",
        "recipe_name": f"{product_model}_{device['station_type']}_R03",
        "inspected_at": iso_time(moment),
    }


def generate_spi_result(path, context, rng):
    pads = []
    result = "PASS"
    for index in range(1, 13):
        volume = round(rng.normalvariate(100.0, 8.5), 2)
        pad_result = "PASS" if 75.0 <= volume <= 125.0 else "NG"
        result = "NG" if pad_result == "NG" else result
        pads.append(
            {
                "pad_id": f"U3-{index:02d}",
                "volume_percent": volume,
                "height_um": round(rng.normalvariate(118.0, 5.0), 2),
                "area_percent": round(rng.normalvariate(98.0, 4.0), 2),
                "result": pad_result,
            }
        )
    payload = {
        "schema_version": "1.0",
        "record_type": "spi_inspection_result",
        "result_id": f"SPI-{context['product_sn']}",
        **context,
        "result": result,
        "cycle_time_ms": rng.randint(780, 1120),
        "thresholds": {"volume_percent": [75.0, 125.0], "height_um": [95.0, 145.0]},
        "pads": pads,
    }
    write_json(path, payload)
    return result, []


def generate_aoi_result(path, image_dir, context, rng):
    is_ng = rng.random() < 0.24
    defects = []
    images = []
    if is_ng:
        defect_code, defect_name = rng.choice(DEFECTS[:4])
        reference = rng.choice(("R18", "C27", "U3", "D5"))
        image_name = f"{context['product_sn']}_{reference}_{defect_code}.png"
        image_path = image_dir / image_name
        write_ng_png(image_path, rng, defect_code, reference)
        defects.append(
            {
                "defect_code": defect_code,
                "defect_name": defect_name,
                "reference": reference,
                "x_mm": round(rng.uniform(12.0, 145.0), 3),
                "y_mm": round(rng.uniform(8.0, 82.0), 3),
                "confidence": round(rng.uniform(0.91, 0.998), 4),
                "image_file": image_name,
            }
        )
        images.append(image_path)
    payload = {
        "schema_version": "1.0",
        "record_type": "aoi_inspection_result",
        "result_id": f"AOI-{context['product_sn']}",
        **context,
        "result": "NG" if is_ng else "PASS",
        "cycle_time_ms": rng.randint(1320, 1850),
        "component_count": 386,
        "defect_count": len(defects),
        "defects": defects,
    }
    write_json(path, payload)
    return payload["result"], images


def generate_fct_report(path, context, rng):
    rows = []
    overall = "PASS"
    tests = (
        ("TP01-TP02", "RESISTANCE", 10000.0, 9000.0, 11000.0, "ohm"),
        ("TP08-GND", "VOLTAGE", 3.30, 3.15, 3.45, "V"),
        ("TP12-GND", "VOLTAGE", 5.00, 4.75, 5.25, "V"),
        ("F1", "CONTINUITY", 0.15, 0.00, 0.50, "ohm"),
    )
    for point, test_name, nominal, lower, upper, unit in tests:
        spread = (upper - lower) / 6.0
        measured = round(rng.normalvariate(nominal, spread), 4)
        result = "PASS" if lower <= measured <= upper else "NG"
        overall = "NG" if result == "NG" else overall
        rows.append((point, test_name, lower, upper, measured, unit, result))
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8-sig", newline="") as target:
        target.write(f"ReportVersion,1.2\n")
        target.write(f"DeviceId,{context['device_id']}\n")
        target.write(f"WorkOrder,{context['work_order']}\n")
        target.write(f"ProductSN,{context['product_sn']}\n")
        target.write(f"TestedAt,{context['inspected_at']}\n")
        target.write(f"OverallResult,{overall}\n")
        writer = csv.writer(target)
        writer.writerow(("TestPoint", "TestName", "LowerLimit", "UpperLimit", "Measured", "Unit", "Result"))
        writer.writerows(rows)
    return overall, []


def write_runtime_log(path, device, context, result, rng):
    path.parent.mkdir(parents=True, exist_ok=True)
    level = "WARN" if result == "NG" else "INFO"
    error_code = "INSPECTION_NG" if result == "NG" else "-"
    temperature = round(rng.uniform(39.0, 48.0), 1)
    with path.open("w", encoding="utf-8") as target:
        target.write(
            f"{context['inspected_at']} level={level} module=inspection "
            f"device={device['device_id']} station={device['station_id']} "
            f"sn={context['product_sn']} result={result} code={error_code} "
            f"temperature_c={temperature}\n"
        )


def write_ipc_log(path, device, context, source_name, rng):
    path.parent.mkdir(parents=True, exist_ok=True)
    request_id = hashlib.sha256(f"{context['product_sn']}:{source_name}".encode()).hexdigest()[:32]
    with path.open("w", encoding="utf-8") as target:
        target.write(
            f"{context['inspected_at']} level=INFO module=collector "
            f"ipc={device['collector_id']} source_device={device['device_id']} "
            f"event=file_detected file={source_name} request_id={request_id} "
            f"queue_depth={rng.randint(0, 4)}\n"
        )


def generate_export_zip(path, device, context, rng):
    export_csv = io.StringIO()
    writer = csv.writer(export_csv)
    writer.writerow(("product_sn", "work_order", "station", "result", "exported_at"))
    for offset in range(5):
        writer.writerow(
            (
                f"{context['product_model'].replace('-', '')}-{datetime.fromisoformat(context['inspected_at']):%y%m%d}-{max(0, int(context['product_sn'].rsplit('-', 1)[1]) - offset):06d}",
                context["work_order"],
                device["station_id"],
                "NG" if rng.random() < 0.08 else "PASS",
                context["inspected_at"],
            )
        )
    manifest = {
        "export_version": "2.1",
        "device_id": device["device_id"],
        "device_model": device["device_model"],
        "software_version": device["software_version"],
        "exported_at": context["inspected_at"],
        "row_count": 5,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("result_export.csv", export_csv.getvalue().encode("utf-8-sig"))
        archive.writestr("manifest.json", json.dumps(manifest, ensure_ascii=False, indent=2))
        archive.writestr("recipe.ini", f"[recipe]\nname={context['recipe_name']}\nrevision=3\n")


def content_type(path):
    return {
        ".json": "application/json",
        ".csv": "text/csv",
        ".png": "image/png",
        ".zip": "application/zip",
        ".log": "text/plain",
    }[path.suffix]


def file_record(run_root, path, file_type, device, context, result=None):
    return {
        "record_id": hashlib.sha256(str(path.relative_to(run_root)).encode()).hexdigest()[:24],
        "file_type": file_type,
        "content_type": content_type(path),
        "line_id": device["line_id"],
        "station_id": device["station_id"],
        "device_id": device["device_id"],
        "collector_id": device["collector_id"],
        "work_order": context["work_order"],
        "product_sn": context.get("product_sn"),
        "result": result,
        "generated_at": context["inspected_at"],
        "relative_path": str(path.relative_to(run_root)),
        "file_size": path.stat().st_size,
        "sha256": sha256_file(path),
    }


def generate_device_cycle(run_root, device, moment, sequence, rng):
    context = base_context(device, moment, sequence)
    date_root = run_root / moment.strftime("%Y/%m/%d") / device["line_id"] / device["station_id"] / device["device_id"]
    stamp = compact_time(moment)
    records = []
    if device["station_type"] == "SPI":
        result_path = date_root / "detection_results" / f"{stamp}_{context['product_sn']}_spi.json"
        result, images = generate_spi_result(result_path, context, rng)
        file_type = "DETECTION_RESULT"
    elif device["station_type"] == "AOI":
        result_path = date_root / "detection_results" / f"{stamp}_{context['product_sn']}_aoi.json"
        result, images = generate_aoi_result(result_path, date_root / "ng_images", context, rng)
        file_type = "DETECTION_RESULT"
    else:
        result_path = date_root / "test_reports" / f"{stamp}_{context['product_sn']}_fct.csv"
        result, images = generate_fct_report(result_path, context, rng)
        file_type = "TEST_REPORT"
    records.append(file_record(run_root, result_path, file_type, device, context, result))
    for image_path in images:
        records.append(file_record(run_root, image_path, "NG_IMAGE", device, context, "NG"))

    device_log = date_root / "runtime_logs" / f"{stamp}_{device['device_id']}.log"
    write_runtime_log(device_log, device, context, result, rng)
    records.append(file_record(run_root, device_log, "RUNTIME_LOG", device, context, result))

    ipc_path = run_root / moment.strftime("%Y/%m/%d") / device["line_id"] / "IPC" / device["collector_id"] / "runtime_logs" / f"{stamp}_{device['device_id']}.log"
    write_ipc_log(ipc_path, device, context, result_path.name, rng)
    records.append(file_record(run_root, ipc_path, "RUNTIME_LOG", device, context))

    if sequence % 5 == 0:
        export_path = date_root / "device_exports" / f"{stamp}_{device['device_id']}_export.zip"
        generate_export_zip(export_path, device, context, rng)
        records.append(file_record(run_root, export_path, "DEVICE_EXPORT", device, context))
    return records


def generate_cycle(run_root, moment, cycle, rng):
    records = []
    base_sequence = cycle * len(DEVICES)
    for offset, device in enumerate(DEVICES):
        records.extend(generate_device_cycle(run_root, device, moment, base_sequence + offset, rng))
    manifest = {
        "schema_version": "1.0",
        "run_id": run_root.name,
        "cycle": cycle,
        "generated_at": iso_time(moment),
        "file_count": len(records),
        "files": records,
    }
    manifest_path = run_root / "_manifests" / f"cycle_{cycle:06d}_{compact_time(moment)}.json"
    write_json(manifest_path, manifest)
    return len(records)


def main():
    args = parse_args()
    start = parse_start_time(args.start_time)
    run_id = f"run_{datetime.now(timezone.utc):%Y%m%dT%H%M%S%fZ}_seed{args.seed}"
    run_root = args.output.resolve() / run_id
    run_root.mkdir(parents=True)
    rng = random.Random(args.seed)
    cycle = 0
    try:
        while args.continuous or cycle < args.cycles:
            moment = start + timedelta(seconds=cycle * args.step_seconds)
            count = generate_cycle(run_root, moment, cycle, rng)
            print(f"cycle={cycle} generated_files={count} business_time={iso_time(moment)}")
            cycle += 1
            if args.continuous:
                time.sleep(args.interval_seconds)
    except KeyboardInterrupt:
        print("收到中断信号，已停止持续生成。")
    print(f"run_root={run_root}")


if __name__ == "__main__":
    main()
