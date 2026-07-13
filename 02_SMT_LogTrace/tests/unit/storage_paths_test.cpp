/**
 * @file storage_paths_test.cpp
 * @brief 验证只读归档目录和可写索引目录边界。
 */

#include "logtrace/storage/storage_paths.h"

#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <string>

namespace smt {
namespace logtrace {
namespace {

/// @brief 为目录测试管理独立临时根目录。
class StoragePathsTest : public testing::Test {
   protected:
    /// @brief 创建测试专用临时目录和归档根目录。
    void SetUp() override {
        char path[] = "/tmp/logtrace-storage-test-XXXXXX";
        char* created = ::mkdtemp(path);
        ASSERT_NE(created, nullptr);
        root_ = created;
        ASSERT_EQ(::mkdir((root_ + "/archive").c_str(), 0750), 0);
    }

    /// @brief 删除测试创建的已知文件和目录。
    void TearDown() override {
        std::remove((root_ + "/index-file").c_str());
        ::rmdir((root_ + "/index/nested").c_str());
        ::rmdir((root_ + "/index").c_str());
        ::rmdir((root_ + "/archive").c_str());
        ::rmdir(root_.c_str());
    }

    std::string root_;
};

TEST_F(StoragePathsTest, CreatesWritableIndexDirectory) {
    StoragePaths paths(StorageConfig{root_ + "/archive", root_ + "/index/nested"});
    paths.initialize();

    EXPECT_TRUE(paths.ready());
    EXPECT_EQ(paths.archiveRoot(), root_ + "/archive");
    EXPECT_EQ(paths.indexRoot(), root_ + "/index/nested");
}

TEST_F(StoragePathsTest, RejectsMissingArchiveDirectory) {
    StoragePaths paths(StorageConfig{root_ + "/missing", root_ + "/index"});
    EXPECT_THROW(paths.initialize(), std::runtime_error);
}

TEST_F(StoragePathsTest, RejectsIndexRegularFile) {
    const std::string file_path = root_ + "/index-file";
    std::ofstream output(file_path.c_str());
    output << "not a directory";
    output.close();

    StoragePaths paths(StorageConfig{root_ + "/archive", file_path});
    EXPECT_THROW(paths.initialize(), std::runtime_error);
}

}  // namespace
}  // namespace logtrace
}  // namespace smt
