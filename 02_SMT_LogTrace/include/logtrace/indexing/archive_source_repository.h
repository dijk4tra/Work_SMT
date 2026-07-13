/**
 * @file archive_source_repository.h
 * @brief 声明一期归档元数据只读仓储。
 */

#ifndef LOGTRACE_INDEXING_ARCHIVE_SOURCE_REPOSITORY_H_
#define LOGTRACE_INDEXING_ARCHIVE_SOURCE_REPOSITORY_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "logtrace/indexing/index_models.h"
#include "logtrace/storage/mysql_client.h"

namespace smt {
namespace logtrace {

/// @brief 按稳定 archive_id 从一期数据库读取可解析归档。
class ArchiveSourceRepository {
   public:
    /// @brief 保存一期只读 MySQL 客户端和查询超时。
    /// @param mysql 一期 MySQL 客户端。
    /// @param timeout_ms 查询观察超时毫秒数。
    ArchiveSourceRepository(const MySqlClient& mysql, int timeout_ms);

    /// @brief 查询指定游标后的 RUNTIME_LOG 和 TEST_REPORT。
    /// @param after_archive_id 已处理的最大归档编号。
    /// @param limit 最大返回文件数。
    /// @param archives 成功时接收按 archive_id 升序排列的记录。
    /// @return 查询和返回字段均有效时为 true。
    bool listAfter(std::uint64_t after_archive_id, std::size_t limit,
                   std::vector<ArchiveRecord>* archives) const;

    /// @brief 按编号读取明确重建队列中的归档。
    /// @param archive_ids 待读取编号，必须非空且已排序。
    /// @param archives 成功时接收完整归档记录。
    /// @return 所有编号存在且字段有效时为 true。
    bool findByIds(const std::vector<std::uint64_t>& archive_ids,
                   std::vector<ArchiveRecord>* archives) const;

   private:
    const MySqlClient& mysql_;
    int timeout_ms_;
};

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_INDEXING_ARCHIVE_SOURCE_REPOSITORY_H_
