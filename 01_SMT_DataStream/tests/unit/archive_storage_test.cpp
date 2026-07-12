/**
 * @file archive_storage_test.cpp
 * @brief 验证非页对齐窗口 mmap 校验、原子移动和摘要拒绝。
 */

#include "datastream/archive/archive_storage.h"

#include <gtest/gtest.h>
#include <unistd.h>

#include <fstream>
#include <string>

#include "datastream/auth/crypto.h"

namespace smt {
namespace datastream {
namespace {

UploadSession makeSession(const StoragePaths& paths, const std::string& id,
                          const std::string& content) {
    UploadSession session;
    session.upload_id = id;
    session.line_id = "LINE-01";
    session.station_id = "ST-AOI-01";
    session.device_id = "AOI-VT-01";
    session.collector_id = "IPC-L01-01";
    session.work_order = "WO-TEST";
    session.product_sn = "SN-TEST";
    session.file_type = "NG_IMAGE";
    session.result = "NG";
    session.original_filename = "board.png";
    session.extension = "png";
    session.temp_path = paths.tempRoot() + "/" + id + ".part";
    session.produced_at = "2026-07-12T00:00:00.000Z";
    session.file_size = content.size();
    session.file_sha256 = sha256Hex(content);
    std::ofstream output(session.temp_path.c_str(), std::ios::binary);
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.close();
    return session;
}

TEST(ArchiveStorageTest, HashesAcrossUnalignedWindowsAndMovesAtomically) {
    char root_template[] = "/tmp/datastream-archive-test-XXXXXX";
    const char* root = ::mkdtemp(root_template);
    ASSERT_NE(root, nullptr);
    StoragePaths paths(std::string(root) + "/temp", std::string(root) + "/archive");
    paths.initialize();
    const std::string content(9001, 'A');
    UploadSession session = makeSession(paths, "123e4567-e89b-42d3-a456-426614174000", content);
    const std::string relative = buildArchiveRelativePath(session, 1783814592456LL);
    const ArchiveStorageResult result = verifyAndArchiveFile(paths, session, relative, 4097);
    EXPECT_EQ(result.status, ArchiveStorageStatus::Archived);
    EXPECT_EQ(::access(session.temp_path.c_str(), F_OK), -1);
    EXPECT_EQ(::access((paths.archiveRoot() + "/" + relative).c_str(), F_OK), 0);
}

TEST(ArchiveStorageTest, RejectsWholeFileDigestMismatch) {
    char root_template[] = "/tmp/datastream-archive-test-XXXXXX";
    const char* root = ::mkdtemp(root_template);
    ASSERT_NE(root, nullptr);
    StoragePaths paths(std::string(root) + "/temp", std::string(root) + "/archive");
    paths.initialize();
    UploadSession session =
        makeSession(paths, "123e4567-e89b-42d3-a456-426614174001", std::string(5000, 'B'));
    session.file_sha256.assign(64, '0');
    const std::string relative = buildArchiveRelativePath(session, 1783814592456LL);
    const ArchiveStorageResult result = verifyAndArchiveFile(paths, session, relative, 4096);
    EXPECT_EQ(result.status, ArchiveStorageStatus::IntegrityMismatch);
    EXPECT_EQ(::access(session.temp_path.c_str(), F_OK), 0);
}

TEST(ArchiveStorageTest, RejectsStatSizeMismatch) {
    char root_template[] = "/tmp/datastream-archive-test-XXXXXX";
    const char* root = ::mkdtemp(root_template);
    ASSERT_NE(root, nullptr);
    StoragePaths paths(std::string(root) + "/temp", std::string(root) + "/archive");
    paths.initialize();
    UploadSession session =
        makeSession(paths, "123e4567-e89b-42d3-a456-426614174002", std::string(5000, 'C'));
    ++session.file_size;
    const std::string relative = buildArchiveRelativePath(session, 1783814592456LL);
    const ArchiveStorageResult result = verifyAndArchiveFile(paths, session, relative, 4096);
    EXPECT_EQ(result.status, ArchiveStorageStatus::IntegrityMismatch);
    EXPECT_EQ(::access(session.temp_path.c_str(), F_OK), 0);
}

}  // namespace
}  // namespace datastream
}  // namespace smt
