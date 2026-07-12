# SMT 检测工位数据采集与归档管理平台

面向 SPI/AOI 检测工位、FCT 测试设备和产线工控机，统一接收检测结果、测试报告、
NG 图片、设备导出文件与运行记录，完成设备认证、断点续传、完整性校验、原子归档、
元数据管理、设备状态维护和历史追溯。

项目采用六期连续开发，六期现已全部完成，当前版本为第一期生产终板 `v1.0.0`。

## 第一期已实现

- C++11、CMake、Wfrest、Workflow、OpenSSL、nlohmann/json、spdlog、GoogleTest 工程；
- 严格 JSON 配置和环境变量加载，未知字段、类型、范围及关联约束错误直接阻止启动；
- Workflow MySQL/Redis 具体客户端，无自动重试和备用客户端；
- 临时/归档目录创建、规范化、可写性和同文件系统检查；
- `GET /health/live` 和 `GET /health/ready`；
- SIGINT/SIGTERM 同步等待和 Wfrest 停止流程；
- MySQL 版本化迁移、checksum 防篡改和旧单线 SPI/AOI/ICT 开发种子；
- GoogleTest 单元/集成测试、clang-format、clang-tidy、Sanitizer 和覆盖率构建选项。

## 第二期已实现

- 正文 SHA-256、HMAC-SHA256 v1 规范串和 OpenSSL 常量时间比较；
- 设备认证头、时间窗口、MySQL 设备/工位/产线启用状态校验；
- Redis `SET NX EX` 请求防重放，认证失败按原因返回稳定业务码；
- `POST /api/v1/devices/heartbeat` 严格 JSON 模型，不接受未知字段；
- MySQL `software_version`/`last_seen_at` 更新和 Redis Hash+TTL 原子在线状态写入；
- Operator Bearer Token 常量时间校验能力，供第四期只读接口复用；
- 密码学、时间、心跳模型、错误映射单元测试和真实服务端到端测试脚本。

## 第三期已实现

- 三线 SPI/AOI/FCT 开发种子和采集器—设备启用绑定；
- `POST /api/v1/uploads`、`PUT /api/v1/uploads/{upload_id}/chunks/{chunk_no}` 和
  `GET /api/v1/uploads/{upload_id}`；
- UUIDv4、磁盘安全水位、`posix_fallocate`、全局/设备/采集器会话配额；
- Redis Hash、摘要 Hash、Bitmap 和 Lua 原子状态操作；
- `pwrite` 写满循环、乱序分片、相同摘要幂等和不同摘要冲突保护；
- 三线 SPI/AOI/FCT 固定模拟样本及真实 HTTP 上传端到端测试。

## 第四期已实现

- `POST /api/v1/uploads/{upload_id}/complete`，原子切换 `VERIFYING` 并拒绝后续分片；
- `stat` 大小核对、窗口化 `mmap` 增量 SHA-256、固定安全路径和同文件系统原子 `rename`；
- MySQL `upload_id` 唯一归档记录，以及 `rename` 后入库失败和入库后 Redis 失败的幂等恢复；
- Operator Token 保护的归档列表和详情，组合过滤及 `(archived_at, archive_id)` Base64URL 游标；
- 启动立即执行并按周期运行的严格清理，只删除旧的 UUID `.part` 且 Redis 无会话或为结束态；
- 三线两周期 63 个固定业务文件的真实 HTTP 全量归档和逐条大小、摘要核对。

## 第五期已实现

- C++11 `collector_agent`，严格配置三条线九台设备的目录、IPC 绑定和密钥环境变量；
- 原子改名、连续三个稳定扫描周期和 `.done` 配对标志三种封口判定；
- payload 不可变快照和 JSON 状态文件，使用 `fsync + rename` 实现重启恢复；
- `READY/UPLOADING/COMPLETING/DONE/FAILED` 状态机和服务端进度查询；
- 网络、超时、5xx、配额和缺片的封顶指数退避，永久 4xx 不重试；
- 服务端会话丢失后清除旧 `upload_id`，只为同一本地任务创建新会话；
- 三线并发文件生命周期模拟器和断网、退出重启、Redis 会话丢失 E2E 脚本。

第五期 E2E 已实际验证断网排队、Collector 强退、Redis 会话丢失和重启恢复，18 个有效文件
完成归档回验，2 个异常文件失败隔离。

## 第六期已实现

- Nginx TLS 反向代理、systemd 隔离账户和可写目录、logrotate；
- 迁移、构建、启动、停止、健康、总验收、覆盖率、Sanitizer、Valgrind 和备份脚本；
- OpenAPI、部署运维、备份恢复、故障演练、调用链和面试演示文档；
- 并发心跳/查询负载工具，以及三线 E2E 中的多会话、乱序、幂等完成和故障场景；
- 干净 Debug/Release、54/54 全量 CTest、81.8% 行覆盖率和本机 TLS 部署演练。

## 构建

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DDATASTREAM_BUILD_TESTS=ON \
  -DDATASTREAM_ENABLE_INTEGRATION_TESTS=ON
cmake --build build -j2
```

主要依赖必须已经安装在系统中。CMake 找不到 Workflow、Wfrest、OpenSSL、spdlog、
nlohmann/json 或 GoogleTest 时明确失败，不在配置阶段联网下载替代版本。

## 配置与环境变量

参考 `conf/datastream.env.example` 设置进程环境。服务不会自动读取 `.env` 文件，避免把本地
配置文件误当成可靠的生产密钥来源。启动和数据库脚本至少需要：

```bash
export SMT_DATASTREAM_MYSQL_PASSWORD='<mysql-password>'
export SMT_DATASTREAM_OPERATOR_TOKEN='<at-least-16-characters>'
```

当前本机 Redis 使用原有无密码 `default` 用户，因此示例配置的 Redis `password_env` 为
`null`。部署环境启用 Redis 密码时，改为环境变量名称并在进程环境中提供对应值。

## 数据库迁移与开发种子

```bash
scripts/db.sh migrate --config conf/datastream.example.json
scripts/db.sh seed --config conf/datastream.example.json
```

迁移记录文件 SHA-256；同一版本文件被修改后脚本会停止。服务进程不会自行修改表结构。
`dev_seed.sql` 中的设备密钥仅供本地开发，不能用于生产环境。

上传配置还包含临时目录最小剩余字节/比例、全局及设备/采集器在途会话数和全局预留字节上限。
这些值是 `docs/模拟业务基线.md` 的仿真假设，不是经过真实工厂确认的生产容量。

## 运行与健康检查

```bash
./build/datastream_server --config conf/datastream.example.json
```

另一个终端执行：

```bash
curl -i http://127.0.0.1:8080/health/live
curl -i http://127.0.0.1:8080/health/ready
```

readiness 只返回总体 `ready/not_ready`，不泄露连接串、路径或依赖错误详情。使用
`SIGINT` 或 `SIGTERM` 停止服务。

参考采集程序使用独立配置和设备密钥环境变量：

```bash
./build/collector_agent --config conf/collector.example.json
```

配置样例见 `conf/collector.example.json` 和 `conf/collector.env.example`。设备 sidecar 与封口规则见
[`docs/采集端与文件生命周期契约.md`](docs/采集端与文件生命周期契约.md)。

设备心跳必须按 [`docs/API契约.md`](docs/API契约.md) 生成五个认证请求头。开发种子密钥和
Python 签名参考实现位于 `migrations/dev_seed.sql` 与 `tests/e2e/heartbeat_e2e_test.py`，仅用于
本地联调。

## 测试与检查

```bash
ctest --test-dir build --output-on-failure
clang-format --dry-run --Werror $(find include src tests -type f \
  \( -name '*.h' -o -name '*.cpp' \) | sort)
clang-tidy -p build $(find src -type f -name '*.cpp' | sort)
```

集成测试实际连接配置中的 MySQL 和 Redis，因此运行前必须提供与服务相同的环境变量。
只运行不访问本机服务的测试可使用：

```bash
ctest --test-dir build -LE integration --output-on-failure
```

## 模拟业务数据

批量生成：

```bash
python3 tools/generate_mock_data.py --cycles 12 --seed 20260711
```

持续生成：

```bash
python3 tools/generate_mock_data.py --continuous --interval-seconds 5
```

数据格式和清单说明见 [`data/README.md`](data/README.md)。

终板使用三条生产线、每线一台 IPC 和 SPI/AOI/FCT 的固定仿真模型。业务规模与文件行为分别见
[`docs/模拟业务基线.md`](docs/模拟业务基线.md) 和
[`docs/采集端与文件生命周期契约.md`](docs/采集端与文件生命周期契约.md)。

## 文档入口

1. 仓库根目录 `agent.md` 与 `代码注释规范.md`；
2. [`docs/开发思路.md`](docs/开发思路.md)；
3. [`docs/分期实施计划.md`](docs/分期实施计划.md)；
4. [`docs/API契约.md`](docs/API契约.md)；
5. [`docs/数据与状态模型.md`](docs/数据与状态模型.md)；
6. [`docs/编码前准备.md`](docs/编码前准备.md)；
7. [`docs/本机环境检查.md`](docs/本机环境检查.md)。
8. [`docs/第一期开发报告.md`](docs/第一期开发报告.md)。
9. [`docs/第二期开发报告.md`](docs/第二期开发报告.md)。
10. [`docs/模拟业务基线.md`](docs/模拟业务基线.md)。
11. [`docs/采集端与文件生命周期契约.md`](docs/采集端与文件生命周期契约.md)。
12. [`docs/学习与面试准备路线.md`](docs/学习与面试准备路线.md)。
13. [`docs/简历能力与代码证据矩阵.md`](docs/简历能力与代码证据矩阵.md)。
14. [`docs/第三期开发报告.md`](docs/第三期开发报告.md)。
15. [`docs/第四期开发报告.md`](docs/第四期开发报告.md)。
16. [`docs/第五期开发报告.md`](docs/第五期开发报告.md)。
17. [`docs/openapi.yaml`](docs/openapi.yaml)。
18. [`docs/部署与运维手册.md`](docs/部署与运维手册.md)。
19. [`docs/故障演练与排查.md`](docs/故障演练与排查.md)。
20. [`docs/核心调用链与面试演示.md`](docs/核心调用链与面试演示.md)。
21. [`docs/第六期暨终板验收报告.md`](docs/第六期暨终板验收报告.md)。
