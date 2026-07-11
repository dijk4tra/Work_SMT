# SMT 检测工位数据采集与归档管理平台

面向 SPI/AOI 检测工位、ICT 测试台和产线工控机，统一接收检测结果、测试报告、
NG 图片、设备导出文件与运行记录，完成设备认证、断点续传、完整性校验、原子归档、
元数据管理、设备状态维护和历史追溯。

项目采用五期连续开发，五期全部完成后交付第一期生产终板。当前已完成第一期工程骨架和
基础设施，尚未实现设备 HMAC、心跳和上传业务接口。

## 第一期已实现

- C++11、CMake、Wfrest、Workflow、OpenSSL、nlohmann/json、spdlog、GoogleTest 工程；
- 严格 JSON 配置和环境变量加载，未知字段、类型、范围及关联约束错误直接阻止启动；
- Workflow MySQL/Redis 具体客户端，无自动重试和备用客户端；
- 临时/归档目录创建、规范化、可写性和同文件系统检查；
- `GET /health/live` 和 `GET /health/ready`；
- SIGINT/SIGTERM 同步等待和 Wfrest 停止流程；
- MySQL 版本化迁移、checksum 防篡改和 SPI/AOI/ICT 开发种子；
- GoogleTest 单元/集成测试、clang-format、clang-tidy、Sanitizer 和覆盖率构建选项。

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

## 测试与检查

```bash
ctest --test-dir build --output-on-failure
clang-format --dry-run --Werror $(find include src tests -type f \
  \( -name '*.h' -o -name '*.cpp' \) | sort)
clang-tidy -p build $(find src -type f -name '*.cpp' | sort)
```

集成测试实际连接配置中的 MySQL 和 Redis，因此运行前必须提供与服务相同的环境变量。

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

## 文档入口

1. 仓库根目录 `agent.md` 与 `代码注释规范.md`；
2. [`docs/开发思路.md`](docs/开发思路.md)；
3. [`docs/分期实施计划.md`](docs/分期实施计划.md)；
4. [`docs/API契约.md`](docs/API契约.md)；
5. [`docs/数据与状态模型.md`](docs/数据与状态模型.md)；
6. [`docs/编码前准备.md`](docs/编码前准备.md)；
7. [`docs/本机环境检查.md`](docs/本机环境检查.md)。
8. [`docs/第一期开发报告.md`](docs/第一期开发报告.md)。
