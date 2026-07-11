/**
 * @file storage_paths.h
 * @brief 管理上传临时目录和正式归档目录的启动校验。
 */

#ifndef DATASTREAM_STORAGE_STORAGE_PATHS_H_
#define DATASTREAM_STORAGE_STORAGE_PATHS_H_

#include <stdexcept>
#include <string>

namespace smt {
namespace datastream {

/// @brief 文件存储目录不符合运行条件时抛出的异常。
class StorageError : public std::runtime_error {
   public:
    /// @brief 使用明确原因构造存储异常。
    /// @param message 文件系统错误说明。
    explicit StorageError(const std::string& message);
};

/// @brief 创建、规范化并检查临时目录和归档目录。
class StoragePaths {
   public:
    /// @brief 保存待初始化的配置路径。
    /// @param temp_root 上传临时目录。
    /// @param archive_root 正式归档目录。
    StoragePaths(const std::string& temp_root, const std::string& archive_root);

    /// @brief 创建目录并检查可写性和同文件系统约束。
    /// @throws StorageError 当目录无法创建、不可写或跨文件系统时抛出。
    void initialize();

    /// @brief 检查已初始化目录当前是否仍满足运行条件。
    /// @return 两个目录存在、可写且位于同一文件系统时返回 true。
    bool ready() const;

    /// @brief 返回规范化后的临时目录。
    /// @return 临时目录绝对路径的常量引用。
    const std::string& tempRoot() const;

    /// @brief 返回规范化后的正式归档目录。
    /// @return 归档目录绝对路径的常量引用。
    const std::string& archiveRoot() const;

   private:
    std::string configured_temp_root_;
    std::string configured_archive_root_;
    std::string temp_root_;
    std::string archive_root_;
};

}  // namespace datastream
}  // namespace smt

#endif  // DATASTREAM_STORAGE_STORAGE_PATHS_H_
