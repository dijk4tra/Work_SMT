# SMT 双项目 Docker 单机部署

本目录提供 DataStream、LogTrace、MySQL、Redis 和 Nginx 的统一联调环境。它用于固定构建依赖、
验证持久卷和恢复流程，不代表多主机高可用。Collector 默认继续部署在产线 IPC，由本地 spool
吸收服务维护窗口。

## 1. 本机启动

```bash
cd deploy-docker
cp .env.example .env
docker compose up -d --build
scripts/health-check.sh
```

DataStream 和 LogTrace 默认分别监听宿主机回环地址的 `9090`、`9091`；启动前应检查端口冲突，
也可在 `.env` 中通过 `SMT_DATASTREAM_PORT`、`SMT_LOGTRACE_PORT` 调整。本机需要直接调试数据库时使用：

```bash
docker compose -f compose.yaml -f compose.dev.yaml up -d --build
```

仓库内 `secrets/example/` 仅供隔离联调。生产部署必须创建仓库外的受控 Secret 目录，将
`SMT_SECRET_DIR` 指向该目录，并把 `.env` 中 `SMT_APPLY_DEV_SEED` 设为 `0`。生产入口继续使用
现有 TLS Nginx 配置或工厂统一入口；本 Compose 的 HTTP 端口只绑定 `127.0.0.1`。

## 2. 数据边界

- `datastream_data` 同时保存 `/data/upload_tmp` 与 `/data/archive`，保留同文件系统原子 `rename`；
- LogTrace 将整个 DataStream 卷挂载为只读，只从 `/source/archive` 读取正文，并仅通过补充 GID
  `10000` 获得归档读取权限；
- `logtrace_index` 保存可从一期事实重建的解析工件和 READY Segment；
- MySQL 开启 binlog、`sync_binlog=1` 与 `innodb_flush_log_at_trx_commit=1`；
- Redis 开启 AOF everysec 和 `noeviction`，缓存不作为正确性来源；
- 应用容器以固定非 root UID 运行，根文件系统只读，密码和 Token 由 Secret 文件注入。
- MySQL、Redis 和业务服务只连接隔离的内部 `backend` 网络；Nginx 额外连接 `edge` 网络，只有
  Nginx 的两个 HTTP 端口可以绑定宿主机回环地址。

## 3. 备份、校验和恢复

一致备份会先停止入口和三个业务进程，Collector 的失败请求保留在 IPC spool；MySQL/Redis 保持运行。

```bash
scripts/backup.sh 20260715-2200
scripts/verify-backup.sh 20260715-2200
scripts/integrity-check.sh --metadata
scripts/integrity-check.sh --full
```

默认 `backups/` 仍位于当前宿主机，只适合演练。生产 `SMT_BACKUP_ROOT` 必须是异机/NAS/受控备份挂载，
否则不能覆盖宿主机或本地磁盘丢失。

恢复会删除当前两个业务库、正式归档和索引，必须在已隔离的恢复窗口显式确认：

```bash
export SMT_RESTORE_CONFIRM=RESTORE:20260715-2200
scripts/restore.sh 20260715-2200
```

脚本先严格校验五项清单和 manifest，并拒绝 tar 中的绝对路径、`..`、链接及特殊文件；之后恢复两个
MySQL 库、一期归档和二期索引，清除可重建的 Redis 会话/查询缓存，最后启动服务并执行全量
SHA-256 对账。不要在生产环境首次尝试恢复；先在隔离主机完成演练并记录耗时。

本轮已实施内容、实际构建及代表性业务恢复结果见仓库根目录
[`SMT双项目Docker与单实例可靠性实际改进方案.md`](../SMT双项目Docker与单实例可靠性实际改进方案.md)。

## 4. 停止与清理

```bash
docker compose down
```

该命令保留命名卷。`docker compose down --volumes` 会删除 MySQL、Redis、归档和索引数据，只有在
明确销毁联调环境时才能执行。
