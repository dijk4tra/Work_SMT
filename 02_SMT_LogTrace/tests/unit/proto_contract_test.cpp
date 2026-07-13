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

}  // namespace
}  // namespace logtrace
}  // namespace smt
