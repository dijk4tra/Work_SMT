# HTTP API 契约（v1）

## 1. 通用约定

- 业务接口前缀：`/api/v1`；健康检查不使用该前缀。
- JSON 编码：UTF-8，`Content-Type: application/json`。
- 二进制分片：`Content-Type: application/octet-stream`。
- 服务端时间输出：UTC ISO 8601，精度到毫秒，例如 `2026-07-11T00:00:00.123Z`。
- 设备产生时间输入：ISO 8601，必须带 `Z` 或数值时区，服务端转换为 UTC。
- 服务端为每个响应返回 `X-Request-Id`；设备请求优先沿用已验证的 `X-Request-Id`。
- 未列出的 JSON 字段首期拒绝，避免客户端拼写错误被静默忽略。
- JSON 整数不能用字符串代替；枚举值大小写敏感。

统一成功响应：

```json
{
  "code": "OK",
  "message": "success",
  "request_id": "01J2ABCDEFGHJKMNPQRSTVWXYZ",
  "data": {}
}
```

统一失败响应：

```json
{
  "code": "INVALID_ARGUMENT",
  "message": "chunk_no is out of range",
  "request_id": "01J2ABCDEFGHJKMNPQRSTVWXYZ",
  "data": null
}
```

客户端只依赖 `code`，不能解析 `message` 做业务判断。服务端不把 SQL、连接串、绝对路径或
三方库原始错误直接返回客户端。

## 2. 认证

### 2.1 设备 HMAC

以下接口使用设备 HMAC：心跳、创建上传、上传分片、查询上传和完成上传。

必要请求头：

| 请求头 | 格式 |
|---|---|
| `X-Device-Id` | 已登记设备编号 |
| `X-Timestamp` | Unix 秒，十进制 |
| `X-Request-Id` | 16 至 64 个 ASCII 字母、数字、`-`、`_` |
| `X-Content-SHA256` | 请求体 SHA-256，64 字符十六进制小写 |
| `X-Signature` | HMAC-SHA256，64 字符十六进制小写 |

规范串严格为：

```text
v1\n
<HTTP_METHOD>\n
<PATH>\n
<DEVICE_ID>\n
<TIMESTAMP>\n
<REQUEST_ID>\n
<CONTENT_SHA256>
```

`PATH` 是不含 scheme、host 和 query 的原始路由路径。设备接口首期不使用 query 参数，因此
不存在查询参数排序问题。空请求体使用空字节串的 SHA-256。

分片正文摘要就是 `X-Content-SHA256`，同时用于认证、幂等摘要记录和内容校验，不增加第二个
分片摘要请求头。

### 2.2 Operator Token

归档列表和详情使用：

```text
Authorization: Bearer <SMT_DATASTREAM_OPERATOR_TOKEN>
```

服务端对 token 做常量时间比较。首期只有一个部署级 Operator Token，不实现用户、角色和
刷新令牌；TLS 是保护该令牌传输的必要条件。

### 2.3 健康检查

健康检查不认证，但只返回总体状态。readiness 不列出具体失败的连接地址或凭据。

## 3. 字段边界

| 字段 | 约束 |
|---|---|
| `line_id` / `station_id` / `device_id` / `collector_id` | 2 至 64 个大写字母、数字、`_`、`-` |
| `work_order` | 1 至 64 个可打印 ASCII 字符；按文件类型可空 |
| `product_sn` | 1 至 96 个可打印 ASCII 字符；按文件类型可空 |
| `original_filename` | 1 至 255 个合法 UTF-8 字符，不含控制字符、`/`、`\` |
| `file_size` | 1 至 2147483648 字节 |
| `chunk_size` | 1048576 至 8388608 字节 |
| `file_sha256` | 64 字符十六进制小写 |
| `file_type` | `DETECTION_RESULT`、`TEST_REPORT`、`NG_IMAGE`、`DEVICE_EXPORT`、`RUNTIME_LOG` |
| `result` | `PASS`、`NG` 或 `null` |

`DETECTION_RESULT`、`TEST_REPORT` 要求 `product_sn` 和 PASS/NG；`NG_IMAGE` 要求
`product_sn` 且结果固定 NG；`DEVICE_EXPORT`、`RUNTIME_LOG` 的产品 SN 和结果可空。

## 4. 健康检查

### 4.1 `GET /health/live`

只要进程能够处理请求即返回 HTTP 200：

```json
{
  "code": "OK",
  "message": "success",
  "request_id": "服务端请求标识",
  "data": {"status": "alive"}
}
```

### 4.2 `GET /health/ready`

MySQL `SELECT 1`、Redis `PING`、临时目录和归档目录均可用时返回 HTTP 200 和 `ready`。
任一必要依赖不可用时返回 HTTP 503、`SERVICE_NOT_READY` 和 `not_ready`。readiness 检查使用
短超时，不修改业务数据。

## 5. 设备心跳

### `POST /api/v1/devices/heartbeat`

请求：

```json
{
  "collector_id": "IPC-L01-01",
  "software_version": "5.4.7",
  "runtime_status": "RUNNING",
  "work_order": "WO-20260711-001",
  "reported_at": "2026-07-11T08:00:00.000+08:00"
}
```

`runtime_status` 为 `RUNNING`、`IDLE`、`ALARM`。`work_order` 在 IDLE 时可为 `null`。
服务端以接收时间作为 `last_seen_at`，`reported_at` 只用于诊断设备时钟偏差。

响应 HTTP 200：

```json
{
  "code": "OK",
  "message": "success",
  "request_id": "设备请求标识",
  "data": {
    "device_id": "AOI-VT-01",
    "last_seen_at": "2026-07-11T00:00:00.123Z",
    "online": true
  }
}
```

## 6. 创建上传会话

### `POST /api/v1/uploads`

请求：

```json
{
  "station_id": "ST-AOI-01",
  "collector_id": "IPC-L01-01",
  "work_order": "WO-20260711-001",
  "product_sn": "CTRLMBA1-260711-000001",
  "file_type": "NG_IMAGE",
  "result": "NG",
  "original_filename": "board_U3_bridge.png",
  "file_size": 921604,
  "file_sha256": "64位十六进制小写摘要",
  "chunk_size": 1048576,
  "produced_at": "2026-07-11T08:00:30.000+08:00"
}
```

服务端根据文件大小重新计算 `chunk_count`，客户端不提交该字段。设备必须归属提交的工位。

响应 HTTP 201：

```json
{
  "code": "OK",
  "message": "success",
  "request_id": "设备请求标识",
  "data": {
    "upload_id": "550e8400-e29b-41d4-a716-446655440000",
    "state": "UPLOADING",
    "chunk_size": 1048576,
    "chunk_count": 1,
    "expires_at": "2026-07-12T00:00:00.123Z"
  }
}
```

`upload_id` 是服务端使用 OpenSSL 随机源生成的 UUIDv4。

## 7. 上传分片

### `PUT /api/v1/uploads/{upload_id}/chunks/{chunk_no}`

- 正文为原始二进制；
- `chunk_no` 从 0 开始；
- 除最后一片外长度必须等于会话 `chunk_size`；
- 最后一片长度为 `file_size - chunk_no * chunk_size`；
- `Content-Length` 必填且必须与实际正文长度一致；
- HMAC 的 `X-Content-SHA256` 同时是分片幂等摘要。

首次成功响应 HTTP 200：

```json
{
  "code": "OK",
  "message": "success",
  "request_id": "设备请求标识",
  "data": {"chunk_no": 0, "already_completed": false}
}
```

相同编号和摘要重传仍返回 HTTP 200，`already_completed` 为 `true`。相同编号但摘要不同返回
HTTP 409 `CHUNK_CONTENT_CONFLICT`。

## 8. 查询上传进度

### `GET /api/v1/uploads/{upload_id}`

使用空正文摘要签名。响应 HTTP 200：

```json
{
  "code": "OK",
  "message": "success",
  "request_id": "设备请求标识",
  "data": {
    "upload_id": "上传编号",
    "state": "UPLOADING",
    "chunk_count": 4,
    "completed_chunks": [0, 2],
    "missing_chunks": [1, 3],
    "expires_at": "2026-07-12T00:00:00.123Z",
    "failure_code": null
  }
}
```

查询进度不刷新 TTL。设备只能查询自己创建的会话。

## 9. 完成上传

### `POST /api/v1/uploads/{upload_id}/complete`

请求体为空并使用空正文摘要签名。全部分片完成后执行状态切换、大小/摘要校验、原子归档和
元数据入库。

首次或幂等重试成功均返回 HTTP 200：

```json
{
  "code": "OK",
  "message": "success",
  "request_id": "设备请求标识",
  "data": {
    "archive_id": 1024,
    "upload_id": "上传编号",
    "state": "ARCHIVED",
    "archived_at": "2026-07-11T00:03:12.456Z"
  }
}
```

缺片返回 `CHUNKS_INCOMPLETE`；最终大小或 SHA-256 不符返回
`FILE_INTEGRITY_MISMATCH`，会话进入 FAILED，必须创建新会话重新上传。

## 10. 归档列表

### `GET /api/v1/archives`

使用 Operator Token。允许的 query 参数：

- `device_id`、`station_id`、`work_order`、`product_sn`；
- `file_type`、`result`；
- `archived_from`、`archived_to`，UTC ISO 8601；
- `page_size`，默认 50，范围 1 至 100；
- `cursor`，服务端返回的不透明游标。

无精确工单或产品 SN 时必须提供归档时间范围，且单次范围不超过 31 天，避免误触发无界全表
扫描。排序固定为 `archived_at DESC, archive_id DESC`。

响应：

```json
{
  "code": "OK",
  "message": "success",
  "request_id": "服务端请求标识",
  "data": {
    "items": [
      {
        "archive_id": 1024,
        "device_id": "AOI-VT-01",
        "station_id": "ST-AOI-01",
        "work_order": "WO-20260711-001",
        "product_sn": "CTRLMBA1-260711-000001",
        "file_type": "NG_IMAGE",
        "result": "NG",
        "original_filename": "board_U3_bridge.png",
        "file_size": 921604,
        "produced_at": "2026-07-11T00:00:30.000Z",
        "archived_at": "2026-07-11T00:03:12.456Z"
      }
    ],
    "next_cursor": null
  }
}
```

游标是 `(archived_at, archive_id)` 固定二进制布局的 Base64URL 编码；服务端严格校验解码长度
和字段范围。游标不是授权凭据，客户端应将其视为不透明值。最后一页 `next_cursor` 为 `null`。

## 11. 归档详情

### `GET /api/v1/archives/{archive_id}`

使用 Operator Token。返回列表字段以及 `line_id`、`collector_id`、`upload_id`、
`relative_path`、64 字符 `file_sha256`。`relative_path` 只用于受控运维，不构成下载 URL。

记录不存在返回 HTTP 404 `ARCHIVE_NOT_FOUND`。

## 12. HTTP 与业务错误映射

| HTTP | 业务码 |
|---:|---|
| 400 | `INVALID_ARGUMENT`、`INVALID_CURSOR` |
| 401 | `AUTH_REQUIRED`、`SIGNATURE_INVALID`、`TIMESTAMP_EXPIRED`、`OPERATOR_TOKEN_INVALID` |
| 403 | `DEVICE_DISABLED`、`UPLOAD_DEVICE_MISMATCH` |
| 404 | `DEVICE_NOT_FOUND`、`UPLOAD_NOT_FOUND`、`ARCHIVE_NOT_FOUND` |
| 409 | `REQUEST_REPLAYED`、`UPLOAD_STATE_CONFLICT`、`CHUNK_CONTENT_CONFLICT`、`CHUNKS_INCOMPLETE` |
| 413 | `FILE_TOO_LARGE`、`CHUNK_TOO_LARGE` |
| 422 | `FILE_INTEGRITY_MISMATCH` |
| 500 | `STORAGE_IO_ERROR` |
| 503 | `SERVICE_NOT_READY`、`MYSQL_UNAVAILABLE`、`REDIS_UNAVAILABLE` |

业务校验错误、基础设施错误和程序缺陷必须区分。没有对应恢复意义的未知异常不转换成成功或
任意默认数据。
