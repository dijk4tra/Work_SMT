/**
 * @file api_response_test.cpp
 * @brief 验证统一响应、业务码映射和请求标识。
 */

#include "datastream/common/api_response.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include <string>

#include "datastream/common/request_id.h"

namespace smt {
namespace datastream {
namespace {

TEST(ApiResponseTest, BuildsContractShape) {
    const nlohmann::json response =
        makeApiResponse(ErrorCode::Ok, "success", "request-1", nlohmann::json{{"status", "alive"}});

    EXPECT_EQ(response.at("code"), "OK");
    EXPECT_EQ(response.at("message"), "success");
    EXPECT_EQ(response.at("request_id"), "request-1");
    EXPECT_EQ(response.at("data").at("status"), "alive");
}

TEST(ApiResponseTest, MapsReadinessFailure) {
    EXPECT_STREQ(errorCodeName(ErrorCode::ServiceNotReady), "SERVICE_NOT_READY");
    EXPECT_EQ(httpStatus(ErrorCode::ServiceNotReady), 503);
    EXPECT_EQ(httpStatus(ErrorCode::InvalidArgument), 400);
    EXPECT_EQ(httpStatus(ErrorCode::StorageIoError), 500);
}

TEST(ApiResponseTest, GeneratesDistinctServerRequestIds) {
    const std::string first = generateRequestId();
    const std::string second = generateRequestId();

    EXPECT_EQ(first.find("srv-"), 0U);
    EXPECT_EQ(second.find("srv-"), 0U);
    EXPECT_NE(first, second);
}

}  // namespace
}  // namespace datastream
}  // namespace smt
