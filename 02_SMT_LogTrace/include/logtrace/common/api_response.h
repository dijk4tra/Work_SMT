/**
 * @file api_response.h
 * @brief 定义 Gateway 统一 HTTP 响应及错误映射。
 */

#ifndef LOGTRACE_COMMON_API_RESPONSE_H_
#define LOGTRACE_COMMON_API_RESPONSE_H_

#include <wfrest/HttpServer.h>

#include <nlohmann/json.hpp>
#include <string>

namespace smt {
namespace logtrace {

/// @brief 第一阶段稳定 HTTP 业务码。
enum class ErrorCode {
    Ok,                    ///< 请求成功。
    ServiceNotReady,       ///< Search Server 依赖未就绪。
    SearchRpcUnavailable,  ///< SRPC 连接或协议失败。
    SearchRpcTimeout       ///< SRPC 请求超时。
};

/// @brief 返回业务码对应的稳定字符串。
/// @param code 业务码。
/// @return 业务码字符串。
const char* errorCodeName(ErrorCode code);

/// @brief 返回业务码对应的 HTTP 状态。
/// @param code 业务码。
/// @return HTTP 状态码。
int httpStatus(ErrorCode code);

/// @brief 写入统一 JSON 响应和 X-Request-Id。
/// @param response Wfrest 响应对象。
/// @param code 稳定业务码。
/// @param message 诊断消息。
/// @param request_id 请求标识。
/// @param data 响应数据。
void sendApiResponse(wfrest::HttpResp* response, ErrorCode code, const std::string& message,
                     const std::string& request_id, const nlohmann::json& data);

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_COMMON_API_RESPONSE_H_
