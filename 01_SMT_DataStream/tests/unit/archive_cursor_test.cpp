/**
 * @file archive_cursor_test.cpp
 * @brief 验证归档稳定游标的固定布局和严格解码。
 */

#include "datastream/common/archive_cursor.h"

#include <gtest/gtest.h>

namespace smt {
namespace datastream {
namespace {

TEST(ArchiveCursorTest, RoundTripsTimestampAndIdentifier) {
    const ArchiveCursor source{1783814592456LL, 1024};
    const std::string encoded = encodeArchiveCursor(source);
    EXPECT_EQ(encoded.size(), 22U);
    ArchiveCursor decoded;
    ASSERT_TRUE(decodeArchiveCursor(encoded, &decoded));
    EXPECT_EQ(decoded.archived_at_milliseconds, source.archived_at_milliseconds);
    EXPECT_EQ(decoded.archive_id, source.archive_id);
}

TEST(ArchiveCursorTest, RejectsMalformedAndZeroFields) {
    ArchiveCursor cursor;
    EXPECT_FALSE(decodeArchiveCursor("short", &cursor));
    EXPECT_FALSE(decodeArchiveCursor("!!!!!!!!!!!!!!!!!!!!!!", &cursor));
    EXPECT_FALSE(decodeArchiveCursor(encodeArchiveCursor(ArchiveCursor{0, 1}), &cursor));
    EXPECT_FALSE(decodeArchiveCursor(encodeArchiveCursor(ArchiveCursor{1, 0}), &cursor));
}

}  // namespace
}  // namespace datastream
}  // namespace smt
