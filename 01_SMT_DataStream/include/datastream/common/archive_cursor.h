/**
 * @file archive_cursor.h
 * @brief 声明归档稳定分页游标的固定二进制编码。
 */

#ifndef DATASTREAM_COMMON_ARCHIVE_CURSOR_H_
#define DATASTREAM_COMMON_ARCHIVE_CURSOR_H_

#include <cstdint>
#include <string>

namespace smt {
namespace datastream {

/// @brief 归档时间和主键组成的稳定分页位置。
struct ArchiveCursor {
    std::int64_t archived_at_milliseconds;
    std::uint64_t archive_id;
};

/// @brief 将分页位置编码为无填充 Base64URL。
/// @param cursor 分页位置。
/// @return 固定 16 字节布局的 Base64URL 字符串。
std::string encodeArchiveCursor(const ArchiveCursor& cursor);

/// @brief 严格解码归档分页游标。
/// @param value 外部游标字符串。
/// @param cursor 成功时接收分页位置。
/// @return 编码、长度和字段范围均有效时返回 true。
bool decodeArchiveCursor(const std::string& value, ArchiveCursor* cursor);

}  // namespace datastream
}  // namespace smt

#endif  // DATASTREAM_COMMON_ARCHIVE_CURSOR_H_
