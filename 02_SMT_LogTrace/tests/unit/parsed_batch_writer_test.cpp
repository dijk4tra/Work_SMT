/**
 * @file parsed_batch_writer_test.cpp
 * @brief 验证解析工件内容、摘要和原子目录边界。
 */

#include "logtrace/indexing/parsed_batch_writer.h"

#include <gtest/gtest.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

namespace smt {
namespace logtrace {
namespace {

/// @brief 为解析工件测试管理临时索引根目录。
class ParsedBatchWriterTest : public testing::Test {
   protected:
    /// @brief 创建测试专用索引根目录。
    void SetUp() override {
        char path[] = "/tmp/logtrace-writer-test-XXXXXX";
        char* created = ::mkdtemp(path);
        ASSERT_NE(created, nullptr);
        root_ = created;
    }

    /// @brief 删除测试创建的已知目录。
    void TearDown() override {
        ::rmdir((root_ + "/parsed/.building").c_str());
        ::rmdir((root_ + "/parsed").c_str());
        ::rmdir(root_.c_str());
    }

    std::string root_;
};

TEST_F(ParsedBatchWriterTest, PublishesStructuredMetadataWithoutRawBody) {
    ArchiveRecord archive;
    archive.archive_id = 10;
    archive.line_id = "LINE-01";
    archive.station_id = "ST-AOI-01";
    archive.device_id = "AOI-VT-01";
    archive.collector_id = "IPC-L01-01";
    archive.work_order = "WO-1";
    archive.product_sn = "SN-1";
    archive.file_type = "RUNTIME_LOG";
    archive.original_filename = "a.log";
    archive.relative_path = "2026/a.log";
    archive.file_size = 100;
    archive.file_sha256.assign(64, 'a');
    archive.produced_at = "2026-07-13T00:00:00.000Z";
    archive.archived_at = "2026-07-13T00:01:00.000Z";

    ParsedDocument document;
    document.archive_id = 10;
    document.byte_offset = 5;
    document.byte_length = 20;
    document.occurred_at = "2026-07-13T00:00:00.000Z";
    document.archived_at = archive.archived_at;
    document.line_id = archive.line_id;
    document.station_id = archive.station_id;
    document.device_id = archive.device_id;
    document.collector_id = archive.collector_id;
    document.work_order = archive.work_order;
    document.product_sn = archive.product_sn;
    document.source_type = archive.file_type;
    document.level = "ERROR";
    document.module_name = "inspection";
    document.error_code = "INSPECTION_NG";
    document.event_name = "";
    document.term_count = 8;

    const ParsedArchive parsed{archive, ParserProfile{"kv_runtime_v1", 1}, {document}};
    const ParsedBatchWriter writer(root_);
    const ParsedBatchArtifact artifact = writer.write(7, 10, 10, 1, 0, {parsed});

    EXPECT_EQ(artifact.relative_path, "parsed/batch_7");
    EXPECT_EQ(artifact.manifest_sha256.size(), 64U);
    std::ifstream manifest_input((root_ + "/parsed/batch_7/manifest.json").c_str());
    nlohmann::json manifest;
    manifest_input >> manifest;
    EXPECT_EQ(manifest["document_count"], 1);
    EXPECT_EQ(manifest["parsed_file_count"], 1);

    std::ifstream document_input((root_ + "/parsed/batch_7/documents.jsonl").c_str());
    std::string line;
    ASSERT_TRUE(static_cast<bool>(std::getline(document_input, line)));
    EXPECT_EQ(line.find("raw body"), std::string::npos);
    const nlohmann::json stored = nlohmann::json::parse(line);
    EXPECT_EQ(stored["byte_offset"], 5);
    EXPECT_EQ(stored["byte_length"], 20);

    EXPECT_THROW(writer.write(7, 10, 10, 1, 0, {parsed}), std::runtime_error);
    writer.remove(7);
}

}  // namespace
}  // namespace logtrace
}  // namespace smt
