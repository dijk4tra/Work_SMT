#!/usr/bin/env python3

import argparse
import hashlib
import json
from pathlib import Path


def sha256_hex(content):
    return hashlib.sha256(content).hexdigest()


def sql_string(value):
    if value is None:
        return "NULL"
    return "'" + value.replace("\\", "\\\\").replace("'", "''") + "'"


def write_archive(root, record, content, corrupt_hash=False):
    path = root / record["relative_path"]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(content)
    record["file_size"] = len(content)
    record["file_sha256"] = "0" * 64 if corrupt_hash else sha256_hex(content)


def build_records(archive_root):
    records = []

    line1 = (
        "2026-07-13T08:00:00.000+08:00 level=INFO module=inspection device=AOI-VT-01 "
        "station=ST-AOI-01 sn=CTRLMBA1-260713-000001 code=- result=PASS\n"
        "2026-07-13T08:00:01.000+08:00 level=ERROR module=inspection device=AOI-VT-01 "
        "station=ST-AOI-01 sn=CTRLMBA1-260713-000001 code=INSPECTION_NG result=NG\n"
    ).encode("utf-8")
    records.append(
        {
            "archive_id": 1,
            "line_id": "LINE-01",
            "station_id": "ST-AOI-01",
            "device_id": "AOI-VT-01",
            "collector_id": "IPC-L01-01",
            "work_order": "WO-20260713-001",
            "product_sn": "CTRLMBA1-260713-000001",
            "file_type": "RUNTIME_LOG",
            "original_filename": "line1_aoi.log",
            "relative_path": "2026/07/13/LINE-01/ST-AOI-01/AOI-VT-01/line1_aoi.log",
            "produced_at": "2026-07-13 00:00:00.000",
            "archived_at": "2026-07-13 00:01:00.000",
        }
    )
    write_archive(archive_root, records[-1], line1)

    line2 = (
        "2026-07-13T08:01:00.000+08:00 level=INFO module=printer device=SPI-ZM-02 "
        "station=ST-SPI-02 sn=POWERDRVB2-260713-000002 code=- result=PASS\r\n"
        "2026-07-13T08:01:01.000+08:00 level=WARN module=printer device=SPI-ZM-02 "
        "station=ST-SPI-02 sn=POWERDRVB2-260713-000002 code=PASTE_HEIGHT_WARN result=NG\r\n"
    ).encode("utf-8")
    records.append(
        {
            "archive_id": 2,
            "line_id": "LINE-02",
            "station_id": "ST-SPI-02",
            "device_id": "SPI-ZM-02",
            "collector_id": "IPC-L02-01",
            "work_order": "WO-20260713-002",
            "product_sn": "POWERDRVB2-260713-000002",
            "file_type": "RUNTIME_LOG",
            "original_filename": "line2_spi.log",
            "relative_path": "2026/07/13/LINE-02/ST-SPI-02/SPI-ZM-02/line2_spi.log",
            "produced_at": "2026-07-13 00:01:00.000",
            "archived_at": "2026-07-13 00:02:00.000",
        }
    )
    write_archive(archive_root, records[-1], line2)

    line3 = (
        "\ufeffReportVersion,1.2\r\n"
        "DeviceId,ICT-TRI-03\r\n"
        "WorkOrder,WO-20260713-003\r\n"
        "ProductSN,CTRLMBA1-260713-000003\r\n"
        "TestedAt,2026-07-13T08:02:00.000+08:00\r\n"
        "OverallResult,NG\r\n"
        "TestPoint,TestName,LowerLimit,UpperLimit,Measured,Unit,Result\r\n"
        "TP01-TP02,RESISTANCE,9000,11000,10020,ohm,PASS\r\n"
        "TP08-GND,\"VOLTAGE,CORE\",3.15,3.45,3.80,V,NG\r\n"
        "F1,CONTINUITY,0,0.5,0.2,ohm,PASS\r\n"
    ).encode("utf-8")
    records.append(
        {
            "archive_id": 3,
            "line_id": "LINE-03",
            "station_id": "ST-ICT-03",
            "device_id": "ICT-TRI-03",
            "collector_id": "IPC-L03-02",
            "work_order": "WO-20260713-003",
            "product_sn": "CTRLMBA1-260713-000003",
            "file_type": "TEST_REPORT",
            "original_filename": "line3_ict.csv",
            "relative_path": "2026/07/13/LINE-03/ST-ICT-03/ICT-TRI-03/line3_ict.csv",
            "produced_at": "2026-07-13 00:02:00.000",
            "archived_at": "2026-07-13 00:03:00.000",
        }
    )
    write_archive(archive_root, records[-1], line3)

    unknown = (
        "2026-07-13T08:03:00.000+08:00 level=ERROR module=inspection device=UNKNOWN-01 "
        "station=ST-AOI-02 code=UNKNOWN_DEVICE\n"
    ).encode("utf-8")
    records.append(
        {
            "archive_id": 4,
            "line_id": "LINE-02",
            "station_id": "ST-AOI-02",
            "device_id": "UNKNOWN-01",
            "collector_id": "IPC-L02-01",
            "work_order": None,
            "product_sn": None,
            "file_type": "RUNTIME_LOG",
            "original_filename": "unknown.log",
            "relative_path": "2026/07/13/LINE-02/ST-AOI-02/UNKNOWN-01/unknown.log",
            "produced_at": "2026-07-13 00:03:00.000",
            "archived_at": "2026-07-13 00:04:00.000",
        }
    )
    write_archive(archive_root, records[-1], unknown)

    corrupt = (
        "2026-07-13T08:04:00.000+08:00 level=ERROR module=inspection device=AOI-VT-03 "
        "station=ST-AOI-03 code=CAMERA_TIMEOUT\n"
    ).encode("utf-8")
    records.append(
        {
            "archive_id": 5,
            "line_id": "LINE-03",
            "station_id": "ST-AOI-03",
            "device_id": "AOI-VT-03",
            "collector_id": "IPC-L03-01",
            "work_order": None,
            "product_sn": None,
            "file_type": "RUNTIME_LOG",
            "original_filename": "corrupt.log",
            "relative_path": "2026/07/13/LINE-03/ST-AOI-03/AOI-VT-03/corrupt.log",
            "produced_at": "2026-07-13 00:04:00.000",
            "archived_at": "2026-07-13 00:05:00.000",
        }
    )
    write_archive(archive_root, records[-1], corrupt, corrupt_hash=True)
    return records


def write_sql(path, records):
    rows = []
    for record in records:
        rows.append(
            "(" + ",".join(
                [
                    str(record["archive_id"]),
                    sql_string(record["line_id"]),
                    sql_string(record["station_id"]),
                    sql_string(record["device_id"]),
                    sql_string(record["collector_id"]),
                    sql_string(record["work_order"]),
                    sql_string(record["product_sn"]),
                    sql_string(record["file_type"]),
                    sql_string(record["original_filename"]),
                    sql_string(record["relative_path"]),
                    str(record["file_size"]),
                    "UNHEX(" + sql_string(record["file_sha256"]) + ")",
                    sql_string(record["produced_at"]),
                    sql_string(record["archived_at"]),
                ]
            ) + ")"
        )
    sql = (
        "INSERT INTO archive_file(archive_id,line_id,station_id,device_id,collector_id,work_order,"
        "product_sn,file_type,original_filename,relative_path,file_size,file_sha256,produced_at,"
        "archived_at) VALUES\n    " + ",\n    ".join(rows) + ";\n"
    )
    path.write_text(sql, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    archive_root = output / "archive"
    archive_root.mkdir()
    records = build_records(archive_root)
    write_sql(output / "archive_rows.sql", records)
    manifest = {
        "records": records,
        "expected": {
            "source_file_count": 5,
            "parsed_file_count": 3,
            "failed_file_count": 2,
            "document_count": 7,
        },
    }
    (output / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )


if __name__ == "__main__":
    main()
