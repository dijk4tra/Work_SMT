# 模拟原始业务数据说明

`mock_inbox/` 同时保存历史单线样本和三线固定回归样本。它不是平台归档目录；第五期
`collector_agent` 使用独立的 `device_inbox` 场景目录，不读取固定样本 manifest 决定采集。

## 目标业务对象

| 对象 | 编号 | 产生的数据 |
|---|---|---|
| SPI 检测工位 | 每线 `ST-SPI-0N` / `SPI-ZM-0N` | 焊膏检测 JSON、设备运行记录、设备导出包 |
| AOI 检测工位 | 每线 `ST-AOI-0N` / `AOI-VT-0N` | 外观检测 JSON、NG 图片、设备运行记录、设备导出包 |
| FCT 测试工位 | 每线 `ST-FCT-0N` / `FCT-TRI-0N` | 功能测试 CSV 报告、设备运行记录、设备导出包 |
| 产线工控机 | `IPC-L01-01` 至 `IPC-L03-01` | 文件发现与采集队列运行记录 |

样本中的设备型号和软件版本用于构造合理数据，不表示与对应厂商存在合作或数据来源关系。
目标终板基线已经调整为三条线、每线一台 IPC 及 SPI/AOI/FCT；现有 ICT 文件在增强生成器和
第三期 FCT 迁移完成前保留为历史回归输入，不能代表最终拓扑。

## 文件类型

| `file_type` | 格式 | 关键内容 |
|---|---|---|
| `DETECTION_RESULT` | UTF-8 JSON | 工单、产品 SN、配方、工位、检测项、结果、缺陷 |
| `TEST_REPORT` | UTF-8 BOM CSV | FCT 测点、上下限、实测值、单位和单项/总结果 |
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
    ├── ST-FCT-01/FCT-TRI-01/
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

生成第五期文件生命周期场景：

```bash
python3 tools/simulate_device_lifecycle.py \
  --root data/device_inbox \
  --include-anomalies
```

该脚本并发产生原子改名、追加后稳定、完成标志、重复内容不同路径、设备导出、运行日志、
零字节、损坏 sidecar 和遗留临时文件，并在根目录写独立 `expected_manifest.json`。manifest
只供 E2E 核对，采集程序只读取业务文件及其
`.meta.json/.done` 配对文件。

## 已知数据边界

- 当前同时保留单线 SPI/AOI/ICT 历史回归样本，以及三线 SPI/AOI/FCT 第三期固定样本；
- AOI 按概率产生 NG 与对应图片；SPI 和 FCT 测量值由限定分布生成，可能自然产生 NG。
- 数据不包含真实人员、客户、供应商或生产机密，可安全用于本地开发。
- 固定数据生成器负责可复现业务内容；生命周期模拟器负责三种封口、追加、延迟关闭和异常文件；
- 三线 FCT 目标、节拍和容量见 `docs/模拟业务基线.md`；文件追加、封口、覆盖和异常生命周期见
  `docs/采集端与文件生命周期契约.md`。
