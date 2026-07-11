/**
 * @file storage_paths_test.cpp
 * @brief 验证存储目录初始化和路径边界。
 */

#include "datastream/storage/storage_paths.h"

#include <gtest/gtest.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <string>

namespace smt {
namespace datastream {
namespace {

/// @brief 为存储目录测试管理独立临时根目录。
class StoragePathsTest : public testing::Test {
   protected:
    /// @brief 创建测试专用临时目录。
    void SetUp() override {
        char path[] = "/tmp/datastream-storage-test-XXXXXX";
        char* created = ::mkdtemp(path);
        ASSERT_NE(created, nullptr);
        root_ = created;
    }

    /// @brief 删除测试创建的已知文件和目录。
    void TearDown() override {
        std::remove((root_ + "/not-a-directory").c_str());
        ::rmdir((root_ + "/upload/tmp").c_str());
        ::rmdir((root_ + "/upload").c_str());
        ::rmdir((root_ + "/archive").c_str());
        ::rmdir(root_.c_str());
    }

    std::string root_;
};

TEST_F(StoragePathsTest, CreatesNestedDirectoriesOnSameFileSystem) {
    StoragePaths paths(root_ + "/upload/tmp", root_ + "/archive");
    paths.initialize();

    EXPECT_TRUE(paths.ready());
    EXPECT_EQ(paths.tempRoot().find(root_), 0U);
    EXPECT_EQ(paths.archiveRoot().find(root_), 0U);
}

TEST_F(StoragePathsTest, RejectsParentTraversal) {
    StoragePaths paths(root_ + "/../outside", root_ + "/archive");
    EXPECT_THROW(paths.initialize(), StorageError);
}

TEST_F(StoragePathsTest, RejectsExistingRegularFile) {
    const std::string file_path = root_ + "/not-a-directory";
    std::ofstream output(file_path.c_str());
    output << "data";
    output.close();

    StoragePaths paths(file_path, root_ + "/archive");
    EXPECT_THROW(paths.initialize(), StorageError);
}

}  // namespace
}  // namespace datastream
}  // namespace smt
