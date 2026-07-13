/**
 * @file storage_paths.h
 * @brief 声明一期归档目录和二期索引目录检查接口。
 */

#ifndef LOGTRACE_STORAGE_STORAGE_PATHS_H_
#define LOGTRACE_STORAGE_STORAGE_PATHS_H_

#include <stdexcept>
#include <string>

#include "logtrace/config/app_config.h"

namespace smt {
namespace logtrace {

/// @brief 归档相对路径无法安全解析时携带稳定失败码的异常。
class ArchivePathError : public std::runtime_error {
   public:
    /// @brief 使用稳定失败码和非敏感说明构造异常。
    /// @param code 归档失败码字面量。
    /// @param message 不包含原始正文的说明。
    ArchivePathError(const char* code, const std::string& message);

    /// @brief 返回稳定归档失败码。
    /// @return 失败码。
    const std::string& code() const;

   private:
    std::string code_;
};

/// @brief 管理只读归档根目录和可写索引根目录。
class StoragePaths {
   public:
    /// @brief 保存目录配置。
    /// @param config 存储目录配置。
    explicit StoragePaths(const StorageConfig& config);

    /// @brief 创建索引目录并严格验证两个目录权限与类型。
    /// @throws std::runtime_error 当目录不满足启动契约时抛出。
    void initialize();

    /// @brief 检查运行期目录是否仍满足读写契约。
    /// @return 归档目录可读且索引目录可写时为 true。
    bool ready() const;

    /// @brief 返回归档根目录。
    /// @return 配置中的归档根目录。
    const std::string& archiveRoot() const;

    /// @brief 返回索引根目录。
    /// @return 配置中的索引根目录。
    const std::string& indexRoot() const;

    /// @brief 安全解析一期归档相对路径并拒绝越界或符号链接逃逸。
    /// @param relative_path 一期数据库保存的归档相对路径。
    /// @return 位于真实归档根目录下的绝对普通文件路径。
    /// @throws ArchivePathError 当路径非法、不存在或逃逸归档根目录时抛出。
    std::string resolveArchiveFile(const std::string& relative_path) const;

   private:
    std::string archive_root_;
    std::string index_root_;
};

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_STORAGE_STORAGE_PATHS_H_
