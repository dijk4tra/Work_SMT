/**
 * @file index_models.h
 * @brief 定义增量扫描、解析文档和批次结果模型。
 */

#ifndef LOGTRACE_INDEXING_INDEX_MODELS_H_
#define LOGTRACE_INDEXING_INDEX_MODELS_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace smt {
namespace logtrace {

/// @brief 一期 archive_file 中可索引归档的只读元数据。
struct ArchiveRecord {
    std::uint64_t archive_id;
    std::string line_id;
    std::string station_id;
    std::string device_id;
    std::string collector_id;
    std::string work_order;
    std::string product_sn;
    std::string file_type;
    std::string original_filename;
    std::string relative_path;
    std::uint64_t file_size;
    std::string file_sha256;
    std::string produced_at;
    std::string archived_at;
};

/// @brief 设备和文件类型对应的固定解析器配置。
struct ParserProfile {
    std::string name;
    unsigned int version;
};

/// @brief 一条日志行或测试点的结构化文档元数据。
struct ParsedDocument {
    std::uint64_t archive_id;
    std::uint64_t byte_offset;
    std::uint64_t byte_length;
    std::string occurred_at;
    std::string archived_at;
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
    std::size_t term_count;
};

/// @brief 单个归档的解析结果和首个稳定失败位置。
struct ArchiveParseResult {
    bool success;
    std::string failure_code;
    std::uint64_t failure_line;
    std::vector<ParsedDocument> documents;
};

/// @brief 已解析归档及其源事实和解析器版本。
struct ParsedArchive {
    ArchiveRecord archive;
    ParserProfile profile;
    std::vector<ParsedDocument> documents;
};

/// @brief 一次增量扫描的可观测汇总。
struct ScanSummary {
    bool batch_created;
    std::uint64_t batch_id;
    std::size_t source_file_count;
    std::size_t parsed_file_count;
    std::size_t failed_file_count;
    std::size_t document_count;
};

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_INDEXING_INDEX_MODELS_H_
