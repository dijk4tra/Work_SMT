/**
 * @file segment_models.h
 * @brief 定义解析工件、不可变 Segment 和查询快照使用的数据模型。
 */

#ifndef LOGTRACE_INDEXING_SEGMENT_MODELS_H_
#define LOGTRACE_INDEXING_SEGMENT_MODELS_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "logtrace/indexing/index_models.h"

namespace smt {
namespace logtrace {

/// @brief 状态库中等待构建 Segment 的 PARSED 批次。
struct ParsedBatchDescriptor {
    std::uint64_t batch_id;
    std::uint64_t first_archive_id;
    std::uint64_t last_archive_id;
    std::size_t source_file_count;
    std::size_t document_count;
    std::string parsed_path;
    std::string parsed_sha256;
};

/// @brief 状态库中允许进入查询快照的 READY Segment。
struct ReadySegmentDescriptor {
    std::uint64_t batch_id;
    std::string segment_name;
    std::string segment_sha256;
};

/// @brief 解析工件中的成功归档和文档计数。
struct ParsedArtifactFile {
    ArchiveRecord archive;
    ParserProfile profile;
    std::size_t document_count;
};

/// @brief 解析工件中的局部文档编号和结构化字段。
struct ParsedArtifactDocument {
    std::uint32_t local_id;
    ParsedDocument document;
};

/// @brief 已完整校验的第二阶段解析工件。
struct ParsedBatchData {
    ParsedBatchDescriptor descriptor;
    std::size_t parsed_file_count;
    std::size_t failed_file_count;
    std::vector<ParsedArtifactFile> files;
    std::vector<ParsedArtifactDocument> documents;
};

/// @brief Segment 文件表中用于原文定位的不可变记录。
struct SegmentFileRecord {
    std::uint64_t archive_id;
    std::uint64_t file_size;
    std::string file_sha256;
    std::string relative_path;
};

/// @brief Segment 文档表中的结构化元数据。
struct SegmentDocumentRecord {
    std::uint64_t doc_id;
    std::uint32_t local_id;
    std::uint32_t file_ordinal;
    std::uint64_t archive_id;
    std::uint64_t byte_offset;
    std::uint64_t byte_length;
    std::int64_t occurred_at_ms;
    std::int64_t archived_at_ms;
    std::uint32_t term_count;
    std::string line_id;
    std::string station_id;
    std::string device_id;
    std::string collector_id;
    std::string work_order;
    std::string product_sn;
    std::string source_type;
    std::string level;
    std::string module_name;
    std::string error_code;
    std::string event_name;
};

/// @brief Posting 表中一个词项在单个文档的词频。
struct SegmentPosting {
    std::uint32_t local_id;
    std::uint32_t term_frequency;
};

/// @brief Term 字典中指向连续 Posting 范围的记录。
struct SegmentTermRecord {
    std::string term;
    std::uint32_t document_frequency;
    std::uint64_t posting_begin;
    std::uint32_t posting_count;
};

/// @brief 已校验并加载到内存的不可变 Segment。
struct LoadedSegment {
    std::uint32_t format_version;
    std::uint64_t batch_id;
    std::uint64_t first_archive_id;
    std::uint64_t last_archive_id;
    std::size_t source_file_count;
    std::size_t parsed_file_count;
    std::string segment_name;
    std::string manifest_sha256;
    double average_document_length;
    std::int64_t min_occurred_at_ms;
    std::int64_t max_occurred_at_ms;
    std::vector<SegmentFileRecord> files;
    std::vector<SegmentDocumentRecord> documents;
    std::vector<SegmentPosting> postings;
    std::vector<SegmentTermRecord> terms;
    std::map<std::string, std::size_t> term_lookup;
};

/// @brief 单次 Segment 构建的结果。
struct SegmentBuildResult {
    std::uint64_t batch_id;
    std::string segment_name;
    std::string manifest_sha256;
    std::size_t document_count;
    std::size_t term_count;
    std::size_t posting_count;
};

/// @brief 单次后台 Segment 构建的可观测结果。
struct SegmentBuildSummary {
    bool batch_built;
    SegmentBuildResult result;
};

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_INDEXING_SEGMENT_MODELS_H_
