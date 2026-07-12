/**
 * @file archive_cursor.cpp
 * @brief 实现归档稳定分页游标的 Base64URL 编解码。
 */

#include "datastream/common/archive_cursor.h"

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <vector>

namespace smt {
namespace datastream {

std::string encodeArchiveCursor(const ArchiveCursor& cursor) {
    std::array<unsigned char, 16> bytes;
    const std::uint64_t timestamp = static_cast<std::uint64_t>(cursor.archived_at_milliseconds);
    for (std::size_t index = 0; index < 8; ++index) {
        bytes[index] = static_cast<unsigned char>(timestamp >> ((7 - index) * 8));
        bytes[index + 8] = static_cast<unsigned char>(cursor.archive_id >> ((7 - index) * 8));
    }
    std::array<unsigned char, 25> encoded;
    const int length = EVP_EncodeBlock(encoded.data(), bytes.data(), bytes.size());
    std::string value(reinterpret_cast<const char*>(encoded.data()),
                      static_cast<std::size_t>(length));
    std::replace(value.begin(), value.end(), '+', '-');
    std::replace(value.begin(), value.end(), '/', '_');
    while (!value.empty() && value[value.size() - 1] == '=') {
        value.resize(value.size() - 1);
    }
    return value;
}

bool decodeArchiveCursor(const std::string& value, ArchiveCursor* cursor) {
    if (value.size() != 22) {
        return false;
    }
    std::string padded = value + "==";
    for (std::size_t index = 0; index < value.size(); ++index) {
        const char c = value[index];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '-' || c == '_')) {
            return false;
        }
    }
    std::replace(padded.begin(), padded.end(), '-', '+');
    std::replace(padded.begin(), padded.end(), '_', '/');
    std::array<unsigned char, 18> decoded;
    const int decoded_length =
        EVP_DecodeBlock(decoded.data(), reinterpret_cast<const unsigned char*>(padded.data()),
                        static_cast<int>(padded.size()));
    if (decoded_length != 18) {
        return false;
    }
    std::uint64_t timestamp = 0;
    std::uint64_t archive_id = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        timestamp = (timestamp << 8) | decoded[index];
        archive_id = (archive_id << 8) | decoded[index + 8];
    }
    if (timestamp > static_cast<std::uint64_t>(INT64_MAX) || timestamp == 0 || archive_id == 0) {
        return false;
    }
    cursor->archived_at_milliseconds = static_cast<std::int64_t>(timestamp);
    cursor->archive_id = archive_id;
    return true;
}

}  // namespace datastream
}  // namespace smt
