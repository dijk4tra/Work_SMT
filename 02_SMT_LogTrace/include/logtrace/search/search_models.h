/**
 * @file search_models.h
 * @brief 定义内存检索条件、命中摘要、分页和详情模型。
 */

#ifndef LOGTRACE_SEARCH_SEARCH_MODELS_H_
#define LOGTRACE_SEARCH_SEARCH_MODELS_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "logtrace/indexing/segment_models.h"

namespace smt {
namespace logtrace {

/// @brief 已在 Gateway 边界校验的日志检索条件。
struct SearchQuery {
    std::vector<std::string> keywords;
    std::string line_id;
    std::string station_id;
    std::string device_id;
    std::string work_order;
    std::string product_sn;
    std::vector<std::string> levels;
    std::string module_name;
    std::string error_code;
    bool has_time_range;
    std::int64_t occurred_from_ms;
    std::int64_t occurred_to_ms;
    bool anomaly_only;
    std::size_t offset;
    std::size_t page_size;
};

/// @brief 一个带稳定相关性分数的日志摘要。
struct SearchHit {
    SegmentDocumentRecord document;
    double score;
};

/// @brief 固定快照上的一页检索结果。
struct SearchPage {
    std::uint64_t snapshot_version;
    std::size_t total_hits;
    std::vector<SearchHit> items;
};

/// @brief 日志结构化元数据、所属文件和精确原文。
struct LogDetail {
    SegmentDocumentRecord document;
    SegmentFileRecord file;
    std::string raw;
};

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_SEARCH_SEARCH_MODELS_H_
