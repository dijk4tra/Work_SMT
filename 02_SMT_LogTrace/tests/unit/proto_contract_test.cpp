/**
 * @file proto_contract_test.cpp
 * @brief 验证第一阶段 Protobuf 健康消息契约。
 */

#include <gtest/gtest.h>

#include "logtrace.pb.h"

namespace smt {
namespace logtrace {
namespace {

TEST(ProtoContractTest, RoundTripsHealthMessages) {
    rpc::HealthResponse response;
    response.set_status(rpc::SERVICE_STATUS_READY);
    response.set_code("OK");
    response.set_message("success");

    std::string encoded;
    ASSERT_TRUE(response.SerializeToString(&encoded));

    rpc::HealthResponse decoded;
    ASSERT_TRUE(decoded.ParseFromString(encoded));
    EXPECT_EQ(decoded.status(), rpc::SERVICE_STATUS_READY);
    EXPECT_EQ(decoded.code(), "OK");
    EXPECT_EQ(decoded.message(), "success");
}

TEST(ProtoContractTest, RoundTripsSearchMessages) {
    rpc::SearchLogsRequest request;
    request.set_request_id("request-1");
    request.add_keywords("camera");
    request.mutable_filter()->set_device_id("AOI-VT-01");
    request.set_offset(10);
    request.set_page_size(20);
    std::string encoded;
    ASSERT_TRUE(request.SerializeToString(&encoded));
    rpc::SearchLogsRequest decoded;
    ASSERT_TRUE(decoded.ParseFromString(encoded));
    EXPECT_EQ(decoded.keywords(0), "camera");
    EXPECT_EQ(decoded.filter().device_id(), "AOI-VT-01");
    EXPECT_EQ(decoded.offset(), 10U);
    EXPECT_EQ(decoded.page_size(), 20U);
}

}  // namespace
}  // namespace logtrace
}  // namespace smt
