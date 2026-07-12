/**
 * @file archive_repository.h
 * @brief 声明 MySQL 归档元数据写入和组合查询仓储。
 */

#ifndef DATASTREAM_ARCHIVE_ARCHIVE_REPOSITORY_H_
#define DATASTREAM_ARCHIVE_ARCHIVE_REPOSITORY_H_

#include <workflow/WFTaskFactory.h>

#include <functional>
#include <string>

#include "datastream/archive/archive_model.h"
#include "datastream/storage/mysql_client.h"

namespace smt {
namespace datastream {

/// @brief 单条归档记录查询状态。
enum class ArchiveLookupStatus {
    Found,       ///< 找到且成功解析记录。
    NotFound,    ///< 记录不存在。
    Unavailable  ///< MySQL 失败或返回损坏数据。
};

/// @brief 归档记录插入状态。
enum class ArchiveInsertStatus {
    Inserted,    ///< 插入成功并获得主键。
    Conflict,    ///< upload_id 或相对路径唯一约束冲突。
    Unavailable  ///< MySQL 查询失败。
};

/// @brief 使用固定字段模板访问 archive_file。
class ArchiveRepository {
   public:
    /// @brief 保存 MySQL 客户端和观察超时。
    /// @param mysql MySQL 客户端。
    /// @param timeout_ms 查询观察超时毫秒数。
    ArchiveRepository(const MySqlClient& mysql, int timeout_ms);

    /// @brief 按上传编号查询唯一归档事实。
    /// @param upload_id 上传编号。
    /// @param callback 返回查询状态和记录。
    /// @return 尚未启动的 MySQL 任务。
    WFMySQLTask* createFindByUploadTask(
        const std::string& upload_id,
        const std::function<void(ArchiveLookupStatus, const ArchiveRecord&)>& callback) const;

    /// @brief 按归档主键查询详情。
    /// @param archive_id 归档主键。
    /// @param callback 返回查询状态和记录。
    /// @return 尚未启动的 MySQL 任务。
    WFMySQLTask* createFindByIdTask(
        std::uint64_t archive_id,
        const std::function<void(ArchiveLookupStatus, const ArchiveRecord&)>& callback) const;

    /// @brief 插入一条归档元数据。
    /// @param record 已完成校验并已移动文件的记录。
    /// @param callback 返回插入状态和新主键。
    /// @return 尚未启动的 MySQL 任务。
    WFMySQLTask* createInsertTask(
        const ArchiveRecord& record,
        const std::function<void(ArchiveInsertStatus, std::uint64_t)>& callback) const;

    /// @brief 执行稳定排序和游标分页的组合查询。
    /// @param query 已校验查询条件。
    /// @param callback 返回 MySQL 可用性和结果页。
    /// @return 尚未启动的 MySQL 任务。
    WFMySQLTask* createListTask(
        const ArchiveQuery& query,
        const std::function<void(bool, const ArchivePage&)>& callback) const;

   private:
    const MySqlClient& mysql_;
    int timeout_ms_;
};

}  // namespace datastream
}  // namespace smt

#endif  // DATASTREAM_ARCHIVE_ARCHIVE_REPOSITORY_H_
