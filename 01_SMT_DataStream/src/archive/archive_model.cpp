/**
 * @file archive_model.cpp
 * @brief 实现上传会话到归档记录的确定性转换。
 */

#include "datastream/archive/archive_model.h"

#include "datastream/common/time_utils.h"

namespace smt {
namespace datastream {

ArchiveRecord makeArchiveRecord(const UploadSession& session, const std::string& relative_path,
                                std::int64_t archived_at_milliseconds) {
    ArchiveRecord record;
    record.archive_id = 0;
    record.upload_id = session.upload_id;
    record.line_id = session.line_id;
    record.station_id = session.station_id;
    record.device_id = session.device_id;
    record.collector_id = session.collector_id;
    record.work_order = session.work_order;
    record.product_sn = session.product_sn;
    record.file_type = session.file_type;
    record.result = session.result;
    record.original_filename = session.original_filename;
    record.relative_path = relative_path;
    record.file_size = session.file_size;
    record.file_sha256 = session.file_sha256;
    record.produced_at = session.produced_at;
    record.archived_at = formatUtcMilliseconds(archived_at_milliseconds);
    record.archived_at_milliseconds = archived_at_milliseconds;
    return record;
}

}  // namespace datastream
}  // namespace smt
