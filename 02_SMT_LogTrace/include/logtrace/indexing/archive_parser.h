/**
 * @file archive_parser.h
 * @brief 声明归档完整性回验和固定格式解析接口。
 */

#ifndef LOGTRACE_INDEXING_ARCHIVE_PARSER_H_
#define LOGTRACE_INDEXING_ARCHIVE_PARSER_H_

#include <cstddef>
#include <string>

#include "logtrace/indexing/index_models.h"

namespace smt {
namespace logtrace {

/// @brief 回验文件大小和 SHA-256，并按固定 profile 解析物理行。
/// @param archive 一期归档元数据。
/// @param absolute_path 受控归档根目录下的绝对文件路径。
/// @param profile 已明确选择的解析器。
/// @param max_line_bytes 单条物理记录最大字节数。
/// @return 文件级原子解析结果，失败时不保留部分文档。
ArchiveParseResult parseArchive(const ArchiveRecord& archive, const std::string& absolute_path,
                                const ParserProfile& profile, std::size_t max_line_bytes);

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_INDEXING_ARCHIVE_PARSER_H_
