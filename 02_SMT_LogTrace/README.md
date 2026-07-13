# SMT 检测设备日志索引与故障检索系统

这是一个面向 SMT 检测工位、测试台和产线工控机运维排障的 C++11 项目。系统读取
`01_SMT_DataStream` 已完成校验的归档元数据和不可变文件，对运行日志与 FCT 测试记录进行
增量解析、倒排索引和结构化检索，并在命中后使用 `pread` 回读原始记录。

当前版本已完成第一阶段代码、设计基线和基础设施复验，已确定双进程架构和完整六期计划。
本阶段只实现工程基础设施、Protobuf/SRPC 健康链路、配置、存储检查和数据库迁移，不包含空的
搜索接口或伪索引实现。Debug/Release 构建及完整 17 项测试已经通过，当前等待用户确认第一阶段。

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
└── logs/                 # 本地日志目录
```

第二期实现固定业务样本时再创建 `tools/`，第六期生成部署资产时再创建 `deploy/`，避免在当前
阶段保留空目录或占位文件。

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

根目录 `agent.md` 和 `代码注释规范.md` 对本项目同样生效。

## 6. 第一阶段构建

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

当前已确认共注册并通过 17 项测试，其中 14 项不依赖本机服务，另外 2 项验证真实 MySQL/Redis
客户端，1 项验证 Search Server、Gateway、SRPC 和 HTTP 的双进程 E2E。

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

迁移脚本保存文件 SHA-256；已执行版本被修改后会明确停止。Search Server 不在启动时自动改表。

## 8. 启动顺序

一期归档目录必须已经存在。先启动 Search Server，再启动 Gateway：

```bash
./build/logsearch_server --config conf/logtrace.json
./build/logtrace_gateway --config conf/logtrace.json
scripts/health_check.sh http://127.0.0.1:8081
```

Gateway 启动时会探测 Search Server；Search Server 会探测两个 MySQL、Redis、归档目录和索引
目录。必要依赖不可用时进程明确退出，不以内存假数据继续运行。
