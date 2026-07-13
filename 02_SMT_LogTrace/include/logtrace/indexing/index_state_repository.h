/**
 * @file index_state_repository.h
 * @brief 声明解析批次和归档消费状态仓储。
 */

#ifndef LOGTRACE_INDEXING_INDEX_STATE_REPOSITORY_H_
#define LOGTRACE_INDEXING_INDEX_STATE_REPOSITORY_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "logtrace/indexing/index_models.h"
#include "logtrace/storage/mysql_client.h"

namespace smt {
namespace logtrace {

/// @brief 重建请求的稳定结果。
enum class RebuildStatus {
    Queued,      ///< 已把原批次归档重新置为 PENDING。
    NotFound,    ///< 指定归档尚无二期状态。
    Unavailable  ///< 状态数据库访问失败。
};

/// @brief 访问 parser_profile、index_batch 和 indexed_archive。
class IndexStateRepository {
   public:
    /// @brief 保存二期状态 MySQL 客户端和查询超时。
    /// @param mysql 状态 MySQL 客户端。
    /// @param timeout_ms 查询观察超时毫秒数。
    IndexStateRepository(const MySqlClient& mysql, int timeout_ms);

    /// @brief 加载全部启用解析器配置。
    /// @param profiles 成功时接收以 `device_id\nfile_type` 为键的配置。
    /// @return 查询和字段解析成功时为 true。
    bool loadProfiles(std::map<std::string, ParserProfile>* profiles) const;

    /// @brief 返回显式重建产生的 PENDING 归档编号。
    /// @param limit 最大返回数量。
    /// @param archive_ids 成功时接收升序编号。
    /// @return 查询成功时为 true。
    bool listPendingArchiveIds(std::size_t limit, std::vector<std::uint64_t>* archive_ids) const;

    /// @brief 查询已有消费状态中的最大 archive_id。
    /// @param archive_id 成功时接收最大编号，无记录时为零。
    /// @return 查询成功时为 true。
    bool maxObservedArchiveId(std::uint64_t* archive_id) const;

    /// @brief 创建 PARSING 批次。
    /// @param first_archive_id 批次最小归档编号。
    /// @param last_archive_id 批次最大归档编号。
    /// @param source_file_count 本批纳入状态管理的文件数。
    /// @param batch_id 成功时接收新批次编号。
    /// @return 插入成功并获得主键时为 true。
    bool createBatch(std::uint64_t first_archive_id, std::uint64_t last_archive_id,
                     std::size_t source_file_count, std::uint64_t* batch_id) const;

    /// @brief 把归档绑定到当前批次并置为 PENDING。
    /// @param batch_id 当前批次编号。
    /// @param archive_id 一期归档编号。
    /// @param profile 找到时为解析器配置，否则为空指针。
    /// @return 写入成功时为 true。
    bool attachArchive(std::uint64_t batch_id, std::uint64_t archive_id,
                       const ParserProfile* profile) const;

    /// @brief 标记归档解析成功。
    /// @param archive_id 一期归档编号。
    /// @param document_count 文档数量。
    /// @return 更新成功时为 true。
    bool markArchiveParsed(std::uint64_t archive_id, std::size_t document_count) const;

    /// @brief 标记归档解析失败并保留首个失败行号。
    /// @param archive_id 一期归档编号。
    /// @param failure_code 稳定失败码。
    /// @param failure_line 首个失败物理行，文件级失败时为零。
    /// @return 更新成功时为 true。
    bool markArchiveFailed(std::uint64_t archive_id, const std::string& failure_code,
                           std::uint64_t failure_line) const;

    /// @brief 发布 PARSED 批次及解析工件摘要。
    /// @param batch_id 批次编号。
    /// @param parsed_path 索引根目录下的工件相对路径。
    /// @param parsed_sha256 manifest SHA-256。
    /// @param document_count 成功文档数。
    /// @return 更新成功时为 true。
    bool markBatchParsed(std::uint64_t batch_id, const std::string& parsed_path,
                         const std::string& parsed_sha256, std::size_t document_count) const;

    /// @brief 标记批次失败。
    /// @param batch_id 批次编号。
    /// @param failure_code 稳定失败码。
    /// @return 更新成功时为 true。
    bool markBatchFailed(std::uint64_t batch_id, const std::string& failure_code) const;

    /// @brief 把遗留 PARSING 批次和归档标记为明确失败。
    /// @param batch_ids 成功时接收需要清理工件的批次编号。
    /// @return 查询和全部更新成功时为 true。
    bool recoverInterruptedBatches(std::vector<std::uint64_t>* batch_ids) const;

    /// @brief 将指定归档所属批次全部重新排队。
    /// @param archive_id 用户指定的一期归档编号。
    /// @param batch_id 找到时接收原批次编号。
    /// @return 已排队、不存在或状态库不可用。
    RebuildStatus requestRebuild(std::uint64_t archive_id, std::uint64_t* batch_id) const;

   private:
    const MySqlClient& mysql_;
    int timeout_ms_;
};

/// @brief 生成 parser_profile 查询使用的稳定复合键。
/// @param device_id 一期设备编号。
/// @param file_type 一期文件类型。
/// @return 不与合法字段内容冲突的复合键。
std::string parserProfileKey(const std::string& device_id, const std::string& file_type);

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_INDEXING_INDEX_STATE_REPOSITORY_H_
