/**
 * @file archive_model.h
 * @brief 定义正式归档元数据和历史查询条件。
 */

#ifndef DATASTREAM_ARCHIVE_ARCHIVE_MODEL_H_
#define DATASTREAM_ARCHIVE_ARCHIVE_MODEL_H_

#include <cstdint>
#include <string>
#include <vector>

#include "datastream/common/archive_cursor.h"
#include "datastream/upload/upload_model.h"

namespace smt {
namespace datastream {

/// @brief MySQL 中一条完整归档记录。
struct ArchiveRecord {
    std::uint64_t archive_id;
    std::string upload_id;
    std::string line_id;
    std::string station_id;
    std::string device_id;
    std::string collector_id;
    std::string work_order;
    std::string product_sn;
    std::string file_type;
    std::string result;
    std::string original_filename;
    std::string relative_path;
    std::uint64_t file_size;
    std::string file_sha256;
    std::string produced_at;
    std::string archived_at;
    std::int64_t archived_at_milliseconds;
};

/// @brief 已完成边界校验的归档组合查询。
struct ArchiveQuery {
    std::string device_id;
    std::string station_id;
    std::string work_order;
    std::string product_sn;
    std::string file_type;
    std::string result;
    bool has_archived_from;
    std::int64_t archived_from_milliseconds;
    bool has_archived_to;
    std::int64_t archived_to_milliseconds;
    int page_size;
    bool has_cursor;
    ArchiveCursor cursor;
};

/// @brief 归档列表查询结果。
struct ArchivePage {
    std::vector<ArchiveRecord> items;
    bool has_more;
};

/// @brief 由上传会话生成待插入归档记录。
/// @param session 已校验完成的上传会话。
/// @param relative_path 正式相对路径。
/// @param archived_at_milliseconds 平台归档时间。
/// @return 可写入 MySQL 的归档记录。
ArchiveRecord makeArchiveRecord(const UploadSession& session, const std::string& relative_path,
                                std::int64_t archived_at_milliseconds);

}  // namespace datastream
}  // namespace smt

#endif  // DATASTREAM_ARCHIVE_ARCHIVE_MODEL_H_
