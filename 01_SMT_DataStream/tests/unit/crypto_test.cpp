/**
 * @file crypto_test.cpp
 * @brief 验证设备认证密码学工具和规范串格式。
 */

#include "datastream/auth/crypto.h"

#include <gtest/gtest.h>

namespace smt {
namespace datastream {
namespace {

TEST(CryptoTest, ComputesKnownSha256Vector) {
    EXPECT_EQ(sha256Hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(CryptoTest, ComputesKnownHmacSha256Vector) {
    EXPECT_EQ(hmacSha256Hex("key", "The quick brown fox jumps over the lazy dog"),
              "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8");
}

TEST(CryptoTest, ComparesEqualLengthValuesInConstantTimePrimitive) {
    EXPECT_TRUE(constantTimeEquals("same", "same"));
    EXPECT_FALSE(constantTimeEquals("same", "sand"));
    EXPECT_FALSE(constantTimeEquals("same", "same-longer"));
}

TEST(CryptoTest, BuildsExactV1CanonicalString) {
    EXPECT_EQ(buildDeviceCanonicalString("POST", "/api/v1/devices/heartbeat", "AOI-VT-01",
                                         "1783737600", "REQ_20260711_0001", "digest"),
              "v1\nPOST\n/api/v1/devices/heartbeat\nAOI-VT-01\n1783737600\n"
              "REQ_20260711_0001\ndigest");
}

}  // namespace
}  // namespace datastream
}  // namespace smt
