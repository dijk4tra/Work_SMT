/**
 * @file api_response_test.cpp
 * @brief 验证第一阶段 HTTP 错误映射和请求标识。
 */

#include "logtrace/common/api_response.h"

#include <gtest/gtest.h>

#include <string>

#include "logtrace/common/request_id.h"

namespace smt {
namespace logtrace {
namespace {

TEST(ApiResponseTest, MapsHealthRpcOutcomes) {
    EXPECT_STREQ(errorCodeName(ErrorCode::Ok), "OK");
    EXPECT_STREQ(errorCodeName(ErrorCode::ServiceNotReady), "SERVICE_NOT_READY");
    EXPECT_STREQ(errorCodeName(ErrorCode::SearchRpcUnavailable), "SEARCH_RPC_UNAVAILABLE");
    EXPECT_STREQ(errorCodeName(ErrorCode::SearchRpcTimeout), "SEARCH_RPC_TIMEOUT");
    EXPECT_EQ(httpStatus(ErrorCode::Ok), 200);
    EXPECT_EQ(httpStatus(ErrorCode::ServiceNotReady), 503);
    EXPECT_EQ(httpStatus(ErrorCode::SearchRpcUnavailable), 502);
    EXPECT_EQ(httpStatus(ErrorCode::SearchRpcTimeout), 504);
    EXPECT_EQ(httpStatus(ErrorCode::InvalidArgument), 400);
    EXPECT_EQ(httpStatus(ErrorCode::OperatorTokenInvalid), 401);
    EXPECT_EQ(httpStatus(ErrorCode::LogNotFound), 404);
    EXPECT_EQ(httpStatus(ErrorCode::IndexCorrupted), 500);
    EXPECT_EQ(httpStatus(ErrorCode::MySqlUnavailable), 503);
}

TEST(ApiResponseTest, GeneratesRandomHexRequestIds) {
    const std::string first = generateRequestId();
    const std::string second = generateRequestId();

    EXPECT_EQ(first.size(), 32U);
    EXPECT_EQ(second.size(), 32U);
    EXPECT_NE(first, second);
    EXPECT_EQ(first.find_first_not_of("0123456789abcdef"), std::string::npos);
    EXPECT_EQ(second.find_first_not_of("0123456789abcdef"), std::string::npos);
}

}  // namespace
}  // namespace logtrace
}  // namespace smt
