# SMT 检测工位数据采集与归档管理平台

本项目面向 SPI/AOI 检测工位、ICT 测试台和产线工控机，接收检测结果、测试报告、
NG 图片、设备导出文件与运行记录，完成设备认证、分片上传、完整性校验、文件归档、
元数据管理、设备状态维护和历史数据查询。

当前仓库处于正式编码前的准备阶段，尚未实现服务端业务代码。已完成：

- 仓库级代码生成规则与 C++ 注释约束；
- 已锁定的项目范围、接口、存储模型和验收标准；
- 可重复执行、可持续运行的原始业务数据模拟器；
- 一批用于开发和测试的 JSON、CSV、PNG、ZIP、LOG 样本。

## 目录

```text
01_SMT_DataStream/
├── bin/                    # 构建后的可执行文件，不提交业务源码
├── conf/                   # 配置示例
├── data/
│   ├── mock_inbox/         # 模拟设备和工控机产生的原始文件
│   └── README.md           # 数据格式和生成方式
├── docs/                   # 开发思路与编码前准备材料
├── include/                # 后续 C++ 公共头文件
├── logs/                   # 后续服务运行日志
├── src/                    # 后续 C++ 实现
├── tests/                  # 后续单元和集成测试
└── tools/                  # 开发辅助工具
```

正式编码时，头文件建议放在 `include/datastream/<module>/`，实现放在
`src/<module>/`，模块只按已经确认的职责拆分，不预建空的通用层。

## 生成模拟数据

批量生成 12 个业务周期：

```bash
python3 tools/generate_mock_data.py --cycles 12 --seed 20260711
```

持续模拟产线，每 5 秒产生一个业务周期，按 `Ctrl+C` 停止：

```bash
python3 tools/generate_mock_data.py --continuous --interval-seconds 5
```

各次执行会创建独立的 `run_*` 目录，不会覆盖已有样本。参数与数据格式见
[`data/README.md`](data/README.md)。

## 开发入口

正式编码前先阅读：

1. 仓库根目录的 `agent.md` 与 `代码注释规范.md`；
2. [`docs/开发思路.md`](docs/开发思路.md)；
3. [`docs/分期实施计划.md`](docs/分期实施计划.md)；
4. [`docs/API契约.md`](docs/API契约.md) 与 [`docs/数据与状态模型.md`](docs/数据与状态模型.md)；
5. [`docs/编码前准备.md`](docs/编码前准备.md)；
6. [`docs/本机环境检查.md`](docs/本机环境检查.md)；
7. [`conf/datastream.example.json`](conf/datastream.example.json) 与
   [`conf/datastream.env.example`](conf/datastream.env.example)。
