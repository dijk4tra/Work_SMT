/**
 * @file upload_repository_test.cpp
 * @brief 验证 Redis Bitmap 的分片位序解释。
 */

#include "datastream/upload/upload_repository.h"

#include <gtest/gtest.h>

namespace smt {
namespace datastream {
namespace {

TEST(UploadRepositoryTest, ReadsRedisMostSignificantBitOrdering) {
    const std::string bitmap(1, static_cast<char>(0xa1));
    EXPECT_TRUE(bitmapContains(bitmap, 0));
    EXPECT_FALSE(bitmapContains(bitmap, 1));
    EXPECT_TRUE(bitmapContains(bitmap, 2));
    EXPECT_TRUE(bitmapContains(bitmap, 7));
    EXPECT_FALSE(bitmapContains(bitmap, 8));
}

}  // namespace
}  // namespace datastream
}  // namespace smt
