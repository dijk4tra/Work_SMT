/**
 * @file cleanup_service_test.cpp
 * @brief 验证临时文件清理候选的严格命名和时间边界。
 */

#include "datastream/cleanup/cleanup_service.h"

#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fstream>

namespace smt {
namespace datastream {
namespace {

TEST(CleanupServiceTest, AcceptsOnlyCanonicalUuidPartName) {
    EXPECT_TRUE(isTemporaryUploadFilename("123e4567-e89b-42d3-a456-426614174000.part"));
    EXPECT_FALSE(isTemporaryUploadFilename("123e4567-e89b-42d3-a456-42661417400g.part"));
    EXPECT_FALSE(isTemporaryUploadFilename("123e4567-e89b-42d3-a456-426614174000.tmp"));
    EXPECT_FALSE(isTemporaryUploadFilename("prefix-123e4567-e89b-42d3-a456-426614174000.part"));
}

TEST(CleanupServiceTest, SelectsOnlyOldRegularStrictFiles) {
    char root_template[] = "/tmp/datastream-cleanup-test-XXXXXX";
    const char* root = ::mkdtemp(root_template);
    ASSERT_NE(root, nullptr);
    const std::string old_path = std::string(root) + "/123e4567-e89b-42d3-a456-426614174000.part";
    const std::string unknown_path = std::string(root) + "/manual.part";
    std::ofstream(old_path.c_str()).put('x');
    std::ofstream(unknown_path.c_str()).put('x');
    struct timespec times[2] = {{100, 0}, {100, 0}};
    ASSERT_EQ(::utimensat(AT_FDCWD, old_path.c_str(), times, 0), 0);
    const std::vector<std::string> candidates = findCleanupCandidates(root, 101);
    ASSERT_EQ(candidates.size(), 1U);
    EXPECT_EQ(candidates[0], old_path);
    EXPECT_EQ(::access(unknown_path.c_str(), F_OK), 0);
}

}  // namespace
}  // namespace datastream
}  // namespace smt
