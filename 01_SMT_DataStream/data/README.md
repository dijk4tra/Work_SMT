# 模拟原始业务数据说明

`mock_inbox/` 模拟检测设备和产线工控机尚未上传的平台原始文件。它不是平台归档目录，
后续采集客户端可遍历这些文件并调用上传接口，用于联调设备认证、分片续传、哈希校验、
元数据入库和历史查询。

## 覆盖的业务对象

| 对象 | 编号 | 产生的数据 |
|---|---|---|
| SPI 检测工位 | `ST-SPI-01` / `SPI-ZM-01` | 焊膏检测 JSON、设备运行记录、设备导出包 |
| AOI 检测工位 | `ST-AOI-01` / `AOI-VT-01` | 外观检测 JSON、NG 图片、设备运行记录、设备导出包 |
| ICT 测试台 | `ST-ICT-01` / `ICT-TRI-01` | 电气测试 CSV 报告、设备运行记录、设备导出包 |
| 产线工控机 | `IPC-L01-01`、`IPC-L01-02` | 文件发现与采集队列运行记录 |

样本中的设备型号和软件版本用于构造合理数据，不表示与对应厂商存在合作或数据来源关系。

## 文件类型

| `file_type` | 格式 | 关键内容 |
|---|---|---|
| `DETECTION_RESULT` | UTF-8 JSON | 工单、产品 SN、配方、工位、检测项、结果、缺陷 |
| `TEST_REPORT` | UTF-8 BOM CSV | ICT 测点、上下限、实测值、单位和单项/总结果 |
| `NG_IMAGE` | PNG | PCB 风格图像、红色缺陷框以及缺陷元信息 |
| `DEVICE_EXPORT` | ZIP | 设备结果 CSV、导出清单 JSON 和配方 INI |
| `RUNTIME_LOG` | UTF-8 LOG | 时间、级别、模块、设备、产品、错误码及运行指标 |

每个周期在 `_manifests/` 下生成一份 JSON 清单。清单记录后续上传接口需要的
`line_id`、`station_id`、`device_id`、`collector_id`、`work_order`、`product_sn`、
`file_type`、相对路径、文件大小和 SHA-256，可直接作为联调输入和校验期望值。

运行记录按采集周期封口为独立小文件，避免后续追加导致清单中的大小和哈希失效。

## 目录示例

```text
mock_inbox/run_<UTC时间>_seed<随机种子>/
├── _manifests/
│   └── cycle_000000_<业务时间>.json
└── 2026/07/11/LINE-01/
    ├── ST-AOI-01/AOI-VT-01/
    │   ├── detection_results/
    │   ├── device_exports/
    │   ├── ng_images/
    │   └── runtime_logs/
    ├── ST-ICT-01/ICT-TRI-01/
    │   ├── device_exports/
    │   ├── runtime_logs/
    │   └── test_reports/
    ├── ST-SPI-01/SPI-ZM-01/
    │   ├── detection_results/
    │   ├── device_exports/
    │   └── runtime_logs/
    └── IPC/<工控机编号>/runtime_logs/
```

## 使用方式

以下命令在项目根目录执行。

固定起始时间生成可复现数据：

```bash
python3 tools/generate_mock_data.py \
  --cycles 100 \
  --step-seconds 30 \
  --seed 20260711 \
  --start-time 2026-07-11T08:00:00+08:00
```

持续生成：

```bash
python3 tools/generate_mock_data.py \
  --continuous \
  --step-seconds 30 \
  --interval-seconds 5
```

`--step-seconds` 控制模拟产线时间推进，`--interval-seconds` 只控制持续模式下的真实等待
时间，因此可用较短等待快速模拟较长业务时间。默认输出位置是 `data/mock_inbox`，也可通过
`--output` 指定其他目录。

## 已知数据边界

- 当前只模拟一条产线、三个工位和两台采集工控机；足以覆盖首期接口和存储模型。
- AOI 按概率产生 NG 与对应图片；SPI 和 ICT 测量值由限定分布生成，可能自然产生 NG。
- 数据不包含真实人员、客户、供应商或生产机密，可安全用于本地开发。
- 模拟器只负责产生设备侧文件，不模拟 HTTP 上传行为；上传客户端随正式代码阶段设计。
