# SMT 检测设备日志索引与故障检索系统

这是一个面向 SMT 检测工位、测试台和产线工控机运维排障的 C++11 项目。系统读取
`01_SMT_DataStream` 已完成校验的归档元数据和不可变文件，对运行日志与 FCT 测试记录进行
增量解析、倒排索引和结构化检索，并在命中后使用 `pread` 回读原始记录。

当前版本已完成第一至第五阶段实现。Search Server 能按 `archive_id` 增量消费一期归档，从原子发布的
`PARSED` 工件构建固定版本 Term、Posting、文档和文件表，并只将完整校验且数据库登记为
`READY` 的不可变 Segment 加入查询快照。已提供低 DF 优先 AND、BM25、业务权重、Top-K、
结构化过滤、错误码知识和原文详情 HTTP/RPC 业务链，并使用本地 SLRU 与 Redis 查询缓存减少重复访问。

## 1. 与一期项目的关系

一期负责把设备文件可靠送入正式归档，二期只读消费以下事实：

- `smt_datastream.archive_file` 中的归档元数据；
- DataStream `archive_root` 下的不可变文件正文；
- `RUNTIME_LOG` 和 `TEST_REPORT` 两类可解析文件。

二期不修改一期元数据，不重复实现设备认证、分片上传、归档和 Collector。二期使用独立数据库
`smt_logtrace` 保存索引批次、归档消费状态、解析配置和错误码知识库。

## 2. 目标架构

```text
DataStream archive_file + archive_root
                  |
                  v
          logsearch_server
  增量扫描 / 解析 / Segment 发布 / 查询 / 缓存
                  ^
             SRPC + Protobuf
                  |
          logtrace_gateway
        Wfrest / HTTP / 参数校验
                  ^
                Nginx
```

生产程序只有两个：

- `logsearch_server`：SRPC 服务、后台增量索引和查询引擎；
- `logtrace_gateway`：对外 HTTP 接入及 RPC 错误映射。

## 3. 目标能力

- 按 `archive_id` 增量发现新归档，不使用可能重复或漂移的时间游标；
- 将日志行或 FCT 测试点作为文档，保存 `archive_id + offset + length`；
- 构建不可变增量 Segment，只有完整构建并登记 READY 后才进入查询快照；
- 多关键词 AND 查询按低文档频率词优先求交；
- 使用 BM25、错误码/模块精确命中和日志等级完成排序；
- 使用 Top-K 小顶堆避免对全部候选排序；
- 详情命中后使用 `pread` 回读原始归档，不把正文写入 MySQL；
- 使用本地 SLRU 与 Redis 分层缓存，近期和历史查询采用不同 TTL。

## 4. 目录

```text
02_SMT_LogTrace/
├── conf/                 # 开发/生产配置和环境变量样例
├── data/                 # 固定日志样本；运行时索引不提交
├── docs/                 # 编号设计、契约、报告和学习文档
├── include/logtrace/     # 与 src 对应的 C++ 声明
├── migrations/           # smt_logtrace 版本化迁移和开发种子
├── proto/                # Protobuf 与 SRPC 服务定义
├── scripts/              # 构建、迁移、运行和验收脚本
├── src/                  # Gateway、Search Server 和基础设施实现
├── tests/                # 单元、集成与端到端测试
├── tools/                # 固定三线解析样本生成器
└── logs/                 # 本地日志目录
```

第六期生成部署资产时再创建 `deploy/`，避免在当前阶段保留空目录或占位文件。

## 5. 文档顺序

1. `docs/01_开发思路.md`
2. `docs/02_模拟业务基线.md`
3. `docs/03_实际业务差距与改进思路.md`
4. `docs/04_编码前准备.md`
5. `docs/05_数据与索引模型.md`
6. `docs/06_HTTP与RPC契约.md`
7. `docs/07_openapi.yaml`
8. `docs/08_日志格式与解析契约.md`
9. `docs/09_分期实施计划.md`
10. `docs/10_本机环境检查.md`
11. `docs/11_第一期开发报告.md`
12. `docs/12_第二期开发报告.md`
13. `docs/13_第三期开发报告.md`
14. `docs/14_第四期开发报告.md`

根目录 `agent.md` 和 `代码注释规范.md` 对本项目同样生效。

## 6. 构建与测试

复制 `conf/logtrace.env.example` 为不会提交的 `conf/logtrace.env`，填入本机凭据后在当前 Shell
加载。服务只读取环境变量，不会主动解析该文件：

```bash
set -a
source conf/logtrace.env
set +a
```

```bash
export SMT_LOGTRACE_SOURCE_MYSQL_PASSWORD='<source-readonly-password>'
export SMT_LOGTRACE_STATE_MYSQL_PASSWORD='<state-database-password>'
export SMT_LOGTRACE_OPERATOR_TOKEN='<operator-bearer-token>'

cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DLOGTRACE_BUILD_TESTS=ON \
  -DLOGTRACE_ENABLE_INTEGRATION_TESTS=ON
cmake --build build --parallel 2
```

非集成测试不访问本机服务：

```bash
ctest --test-dir build -LE integration --output-on-failure
```

需要显式构建集成测试时，使用同一构建目录重新配置：

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DLOGTRACE_BUILD_TESTS=ON \
  -DLOGTRACE_ENABLE_INTEGRATION_TESTS=ON
cmake --build build --parallel 2
ctest --test-dir build -N
```

当前共注册并通过 46 项测试，其中 42 项不依赖本机服务，另外 2 项验证真实 MySQL/Redis
客户端，1 项验证 Search Server、Gateway、SRPC 和 HTTP 健康链路，1 项验证增量扫描、失败隔离、
倒排 Segment、中断恢复、损坏拒绝、重建和 Search Server 后台轮询。

## 7. 数据库准备

本地先创建独立数据库，生产环境则由 DBA 创建最小权限账号和库：

```sql
CREATE DATABASE smt_logtrace
    CHARACTER SET utf8mb4
    COLLATE utf8mb4_0900_ai_ci;
```

随后显式迁移和加载仿真配置：

```bash
scripts/db.sh migrate --config conf/logtrace.json
scripts/db.sh seed --config conf/logtrace.json
```

第二、第三阶段分别需要 `002_parsed_batch.sql` 和 `003_ready_segment.sql`。迁移脚本保存文件
SHA-256；已执行版本被修改后会明确停止。
Search Server 不在启动时自动改表。

## 8. 启动顺序

一期归档目录必须已经存在。先启动 Search Server，再启动 Gateway：

```bash
./build/logsearch_server --config conf/logtrace.json
./build/logtrace_gateway --config conf/logtrace.json
scripts/health_check.sh http://127.0.0.1:8081
```

Gateway 启动时会探测 Search Server；Search Server 会探测两个 MySQL、Redis、归档目录和索引
目录，恢复 BUILDING 批次并加载全部 READY Segment，然后启动后台增量轮询。任一 READY
Segment 缺失或损坏时启动明确失败，不以空快照继续运行。

## 9. 第二阶段管理命令

单次扫描适合本机验收和运维排查：

```bash
./build/logtrace_admin --config conf/logtrace.json scan-once
```

明确重建某个已消费归档时使用：

```bash
./build/logtrace_admin --config conf/logtrace.json rebuild --archive-id 123
```

重建按原解析批次处理，避免旧批次留下部分有效工件。Search Server 与管理命令通过
`index_root/.indexer.lock` 跨进程串行化，不能同时修改解析状态。

成功解析的批次位于 `index_root/parsed/batch_<batch_id>/`，包含 `manifest.json`、
`archives.jsonl` 和 `documents.jsonl`。工件只保存归档元数据、结构化字段及精确
`offset/length`，不复制完整原文。第二阶段的成功状态是 `PARSED`；第三阶段构建并原子发布
Segment 后才会进入 `READY` 和查询范围。

固定三线样本可重复生成到一个尚不存在的目录：

```bash
python3 tools/generate_phase2_samples.py --output /tmp/logtrace-phase2-samples
```

## 10. 第三阶段 Segment 命令与布局

单次构建最早的 PARSED 批次：

```bash
./build/logtrace_admin --config conf/logtrace.json build-once
```

正式目录为 `index_root/segments/segment_<batch_id>/`：

```text
manifest.json
terms.bin
postings.bin
documents.bin
files.bin
```

二进制文件使用固定小端版本头和显式长度，manifest 保存每个文件的大小与 SHA-256。构建先在
`segments/.building/segment_<batch_id>` 完成 `fsync` 和整段加载校验，再原子 `rename`。只有后续
MySQL 原子更新为 `READY/INDEXED` 后才会交换内存快照。Segment 不保存完整正文；详情能力从
`files.bin` 和 `documents.bin` 定位一期归档，使用循环 `pread` 回读精确字节。

## 11. 第四阶段业务接口

所有 `/api/v1` 业务路由要求 `Authorization: Bearer <token>`，令牌由
`SMT_LOGTRACE_OPERATOR_TOKEN` 环境变量提供。已实现：

- `POST /api/v1/logs/search`：关键词 AND、结构化过滤、BM25 和 Top-K；
- `GET /api/v1/logs/anomalies`：WARN、ERROR 或带错误码的记录；
- `GET /api/v1/logs/{doc_id}`：结构化详情和 `pread` 原文；
- `GET /api/v1/error-codes/{code}`：错误码知识及最近五条匹配日志。

`offset + page_size` 不得超过 1000。没有精确设备、工单、产品 SN 或错误码时，必须提供不超过
31 天的时间范围。完整契约见 `docs/06_HTTP与RPC契约.md` 和 `docs/07_openapi.yaml`。

## 12. 第五阶段缓存

本地缓存使用线程安全 SLRU。新条目进入 Probation，重复命中后晋升 Protected；Protected 溢出时
降级到 Probation，Probation 溢出再淘汰。缓存内容包括日志详情、所属文件元数据和错误码知识，详情
正文超过 `cache.max_detail_bytes` 时不进入本地缓存。

Redis 只缓存查询页的 `doc_id` 顺序、分数和总命中数，不保存日志正文。Key 形式为
`<key_prefix>query:v1:<snapshot_version>:<sha256>`，摘要输入包含规范化关键词、过滤条件、查询类型和
分页。最近两小时的非空/空结果默认 TTL 为 30/10 秒，已结束历史时间段为 600/300 秒。Redis GET、
SETEX 或缓存解析失败时不重试，当前请求直接使用本地不可变索引并保持结果正确。

固定百万文档并发负载可重复执行：

```bash
./build/tests/logtrace_search_load_test
```

该程序使用单机内存仿真快照验证 100 万文档、8 个并发查询的稳定结果，仅作为算法和线程安全基线，
不外推为真实工厂容量。
