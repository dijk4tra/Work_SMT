# HTTP 与 RPC 契约

## 1. 通用约定

- HTTP JSON 使用 UTF-8，业务前缀为 `/api/v1`；
- HTTP 时间使用 UTC ISO 8601 毫秒格式；
- Gateway 为每个请求生成或传递 `X-Request-Id`；
- SRPC 使用 Protobuf 二进制消息；
- HTTP 和 RPC 均拒绝超出契约的范围，不用默认值掩盖非法输入。

统一 HTTP 响应：

```json
{
  "code": "OK",
  "message": "success",
  "request_id": "...",
  "data": {}
}
```

## 2. 第一阶段已实现契约

### 2.1 `GET /health/live`

Gateway 能处理请求即返回 HTTP 200 和 `alive`，不检查 Search Server。

### 2.2 `GET /health/ready`

Gateway 调用 `LogSearchService.Health`。Search Server 异步检查：

- 一期源 MySQL；
- 二期状态 MySQL；
- Redis；
- 一期归档目录可读；
- 二期索引目录可写。

全部正常时返回 HTTP 200；依赖不可用返回 503；RPC 网络失败返回 502；RPC 超时返回 504。

### 2.3 `LogSearchService.Health`

请求包含 `request_id`。响应状态为 READY 或 NOT_READY，并返回稳定 `code` 与非敏感说明。

## 3. 第四阶段目标 HTTP 接口

以下接口当前只作为已确认外部契约，不在第一阶段注册路由：

| 方法与路径 | 用途 |
|---|---|
| `GET /api/v1/error-codes/{code}` | 错误码说明及匹配日志摘要 |
| `POST /api/v1/logs/search` | 关键词与结构化组合查询 |
| `GET /api/v1/logs/anomalies` | WARN/ERROR 或带错误码记录 |
| `GET /api/v1/logs/{doc_id}` | 摘要、结构化字段和原始记录 |

### 3.1 搜索请求目标字段

```json
{
  "keywords": ["camera", "timeout"],
  "line_id": "LINE-01",
  "station_id": null,
  "device_id": "AOI-VT-01",
  "work_order": null,
  "product_sn": null,
  "levels": ["WARN", "ERROR"],
  "module_name": null,
  "error_code": null,
  "occurred_from": "2026-07-12T00:00:00.000Z",
  "occurred_to": "2026-07-13T00:00:00.000Z",
  "offset": 0,
  "page_size": 50
}
```

关键词为 AND。`offset + page_size` 不得超过 1000。没有精确设备、工单、SN 或错误码时必须提供
有限时间范围，避免无界扫描。

## 4. 目标 RPC 方法

第四阶段在当前 `Health` 之外增加：

- `SearchLogs`；
- `ListAnomalies`；
- `GetLogDetail`；
- `GetErrorCode`。

Protobuf 字段编号发布后不复用。新增字段使用新编号，旧字段语义不能静默改变。

## 5. 错误映射

| HTTP | 业务码 | 场景 |
|---:|---|---|
| 400 | `INVALID_ARGUMENT` | JSON、时间、过滤条件或分页非法 |
| 401 | `OPERATOR_TOKEN_INVALID` | 第四阶段业务接口认证失败 |
| 404 | `LOG_NOT_FOUND`、`ERROR_CODE_NOT_FOUND` | 文档或知识库记录不存在 |
| 409 | `INDEX_SNAPSHOT_CHANGED` | 后续若采用快照游标时版本不一致 |
| 500 | `INDEX_CORRUPTED`、`STORAGE_IO_ERROR` | 索引或文件读取失败 |
| 502 | `SEARCH_RPC_UNAVAILABLE` | Gateway 无法完成 RPC 连接 |
| 503 | `SERVICE_NOT_READY`、`MYSQL_UNAVAILABLE` | Search Server 必要依赖不可用 |
| 504 | `SEARCH_RPC_TIMEOUT` | RPC 超时 |

业务错误、传输错误和程序缺陷必须区分。只读 RPC 首版不自动重试。
