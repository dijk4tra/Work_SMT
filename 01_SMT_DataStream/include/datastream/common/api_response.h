/**
 * @file api_response.h
 * @brief 定义统一 HTTP 业务码与 JSON 响应输出接口。
 */

#ifndef DATASTREAM_COMMON_API_RESPONSE_H_
#define DATASTREAM_COMMON_API_RESPONSE_H_

#include <nlohmann/json.hpp>
#include <string>

namespace wfrest {
class HttpResp;
}

namespace smt {
namespace datastream {

/// @brief HTTP 接口使用的稳定业务错误码。
enum class ErrorCode {
    Ok = 0,                ///< 请求成功。
    InvalidArgument,       ///< 外部参数不符合契约。
    AuthRequired,          ///< 缺少认证信息。
    SignatureInvalid,      ///< 摘要或签名不正确。
    TimestampExpired,      ///< 设备请求时间超出允许窗口。
    RequestReplayed,       ///< 请求标识已在防重放窗口中使用。
    DeviceDisabled,        ///< 设备、工位或产线已停用。
    DeviceNotFound,        ///< 设备未登记。
    OperatorTokenInvalid,  ///< 运维令牌不正确。
    ServiceNotReady,       ///< 必要依赖或目录尚未就绪。
    MySqlUnavailable,      ///< MySQL 当前不可用。
    RedisUnavailable,      ///< Redis 当前不可用。
    StorageIoError         ///< 文件系统操作失败。
};

/// @brief 返回业务错误码的稳定字符串。
/// @param code 业务错误码。
/// @return API 返回使用的错误码字符串。
const char* errorCodeName(ErrorCode code);

/// @brief 返回业务错误码对应的 HTTP 状态码。
/// @param code 业务错误码。
/// @return HTTP 状态码。
int httpStatus(ErrorCode code);

/// @brief 构造统一 JSON 响应对象。
/// @param code 业务错误码。
/// @param message 面向调用方的简短说明。
/// @param request_id 当前请求标识。
/// @param data 响应数据，失败时通常为 null。
/// @return 符合 API 契约的 JSON 对象。
nlohmann::json makeApiResponse(ErrorCode code, const std::string& message,
                               const std::string& request_id, const nlohmann::json& data);

/// @brief 将统一 JSON 响应写入 Wfrest 响应对象。
/// @param response Wfrest 响应对象。
/// @param code 业务错误码。
/// @param message 面向调用方的简短说明。
/// @param request_id 当前请求标识。
/// @param data 响应数据，失败时通常为 null。
void sendApiResponse(wfrest::HttpResp* response, ErrorCode code, const std::string& message,
                     const std::string& request_id, const nlohmann::json& data);

}  // namespace datastream
}  // namespace smt

#endif  // DATASTREAM_COMMON_API_RESPONSE_H_
