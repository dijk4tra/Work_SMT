/**
 * @file archive_parser_test.cpp
 * @brief 验证运行日志、测试报告、字节范围和文件级失败契约。
 */

#include "logtrace/indexing/archive_parser.h"

#include <gtest/gtest.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "logtrace/common/sha256.h"

namespace smt {
namespace logtrace {
namespace {

ArchiveRecord makeArchive(std::uint64_t archive_id, const std::string& file_type,
                          const std::string& content) {
    Sha256 sha256;
    sha256.update(content.data(), content.size());
    ArchiveRecord archive;
    archive.archive_id = archive_id;
    archive.line_id = "LINE-01";
    archive.station_id = file_type == "TEST_REPORT" ? "ST-ICT-01" : "ST-AOI-01";
    archive.device_id = file_type == "TEST_REPORT" ? "ICT-TRI-01" : "AOI-VT-01";
    archive.collector_id = "IPC-L01-01";
    archive.work_order = "WO-20260713-001";
    archive.product_sn = "CTRLMBA1-260713-000001";
    archive.file_type = file_type;
    archive.original_filename = "sample.log";
    archive.relative_path = "sample.log";
    archive.file_size = content.size();
    archive.file_sha256 = sha256.finishHex();
    archive.produced_at = "2026-07-13T00:00:00.000Z";
    archive.archived_at = "2026-07-13T00:01:00.000Z";
    return archive;
}

/// @brief 为解析测试管理独立临时目录和文件。
class ArchiveParserTest : public testing::Test {
   protected:
    /// @brief 创建测试专用临时目录。
    void SetUp() override {
        char path[] = "/tmp/logtrace-parser-test-XXXXXX";
        char* created = ::mkdtemp(path);
        ASSERT_NE(created, nullptr);
        root_ = created;
    }

    /// @brief 删除测试创建的文件和目录。
    void TearDown() override {
        std::remove((root_ + "/sample.bin").c_str());
        ::rmdir(root_.c_str());
    }

    /// @brief 按二进制原样写入当前测试文件。
    /// @param content 文件内容。
    /// @return 临时文件路径。
    std::string writeFile(const std::string& content) const {
        const std::string path = root_ + "/sample.bin";
        std::ofstream output(path.c_str(), std::ios::binary);
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        output.close();
        return path;
    }

    std::string root_;
};

TEST_F(ArchiveParserTest, ParsesRuntimeLinesAndExactOffsets) {
    const std::string first =
        "2026-07-13T08:00:00.000+08:00 level=INFO module=inspection device=AOI-VT-01 "
        "station=ST-AOI-01 sn=CTRLMBA1-260713-000001 code=- result=PASS";
    const std::string second =
        "2026-07-13T08:00:01.000+08:00 level=ERROR module=inspection device=AOI-VT-01 "
        "station=ST-AOI-01 sn=CTRLMBA1-260713-000001 code=INSPECTION_NG result=NG";
    const std::string content = first + "\n" + second + "\n";
    const ArchiveParseResult result =
        parseArchive(makeArchive(1, "RUNTIME_LOG", content), writeFile(content),
                     ParserProfile{"kv_runtime_v1", 1}, 65536);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.documents.size(), 2U);
    EXPECT_EQ(result.documents[0].byte_offset, 0U);
    EXPECT_EQ(result.documents[0].byte_length, first.size());
    EXPECT_EQ(result.documents[1].byte_offset, first.size() + 1);
    EXPECT_EQ(result.documents[1].byte_length, second.size());
    EXPECT_EQ(result.documents[1].occurred_at, "2026-07-13T00:00:01.000Z");
    EXPECT_EQ(result.documents[1].error_code, "INSPECTION_NG");
}

TEST_F(ArchiveParserTest, ExcludesCrLfTerminatorAndAcceptsUtf8) {
    const std::string line =
        "2026-07-13T08:00:00.000+08:00 level=WARN module=camera device=AOI-VT-01 "
        "station=ST-AOI-01 sn=CTRLMBA1-260713-000001 code=CAMERA_TIMEOUT detail=相机超时";
    const std::string content = line + "\r\n";
    const ArchiveParseResult result =
        parseArchive(makeArchive(2, "RUNTIME_LOG", content), writeFile(content),
                     ParserProfile{"kv_runtime_v1", 1}, line.size());

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.documents.size(), 1U);
    EXPECT_EQ(result.documents[0].byte_length, line.size());
}

TEST_F(ArchiveParserTest, RejectsInvalidUtf8AndDiscardsPartialDocuments) {
    std::string invalid =
        "2026-07-13T08:00:00.000+08:00 level=INFO module=inspection device=AOI-VT-01 "
        "station=ST-AOI-01 sn=CTRLMBA1-260713-000001 code=- detail=";
    invalid.push_back(static_cast<char>(0xC0));
    invalid.push_back(static_cast<char>(0xAF));
    invalid.push_back('\n');
    const ArchiveParseResult result =
        parseArchive(makeArchive(3, "RUNTIME_LOG", invalid), writeFile(invalid),
                     ParserProfile{"kv_runtime_v1", 1}, 65536);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.failure_code, "INVALID_UTF8");
    EXPECT_EQ(result.failure_line, 1U);
    EXPECT_TRUE(result.documents.empty());
}

TEST_F(ArchiveParserTest, RejectsLongLineAndShaMismatch) {
    const std::string content =
        "2026-07-13T08:00:00.000+08:00 level=INFO module=inspection device=AOI-VT-01\n";
    ArchiveRecord archive = makeArchive(4, "RUNTIME_LOG", content);
    ArchiveParseResult result =
        parseArchive(archive, writeFile(content), ParserProfile{"kv_runtime_v1", 1}, 32);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.failure_code, "LINE_TOO_LONG");

    archive.file_sha256.assign(64, '0');
    result = parseArchive(archive, root_ + "/sample.bin", ParserProfile{"kv_runtime_v1", 1}, 65536);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.failure_code, "ARCHIVE_SHA256_MISMATCH");

    archive = makeArchive(4, "RUNTIME_LOG", content);
    ++archive.file_size;
    result = parseArchive(archive, root_ + "/sample.bin", ParserProfile{"kv_runtime_v1", 1}, 65536);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.failure_code, "ARCHIVE_SIZE_MISMATCH");
}

TEST_F(ArchiveParserTest, ParsesBomCsvQuotedFieldAndNgNormalization) {
    const std::string prefix = "\xEF\xBB\xBF";
    const std::string data_pass = "TP01-TP02,RESISTANCE,9000,11000,10002,ohm,PASS";
    const std::string data_ng = "TP08-GND,\"VOLTAGE,CORE\",3.15,3.45,3.80,V,NG";
    const std::string content =
        prefix +
        "ReportVersion,1.2\r\nDeviceId,ICT-TRI-01\r\nWorkOrder,WO-20260713-001\r\n"
        "ProductSN,CTRLMBA1-260713-000001\r\nTestedAt,2026-07-13T08:00:00.000+08:00\r\n"
        "OverallResult,NG\r\nTestPoint,TestName,LowerLimit,UpperLimit,Measured,Unit,Result\r\n" +
        data_pass + "\r\n" + data_ng + "\r\n";
    ArchiveRecord archive = makeArchive(5, "TEST_REPORT", content);
    const ArchiveParseResult result =
        parseArchive(archive, writeFile(content), ParserProfile{"fct_csv_v1", 1}, 65536);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.documents.size(), 2U);
    EXPECT_EQ(result.documents[1].level, "ERROR");
    EXPECT_EQ(result.documents[1].module_name, "fct");
    EXPECT_EQ(result.documents[1].error_code, "FCT_LIMIT_FAIL");
    EXPECT_EQ(result.documents[1].event_name, "TP08-GND:VOLTAGE,CORE");
    EXPECT_EQ(result.documents[0].byte_offset, content.find(data_pass));
    EXPECT_EQ(result.documents[0].byte_length, data_pass.size());
}

TEST_F(ArchiveParserTest, RejectsCsvMetadataMismatchAndMalformedRow) {
    const std::string header =
        "ReportVersion,1.2\nDeviceId,ICT-TRI-99\nWorkOrder,WO-20260713-001\n"
        "ProductSN,CTRLMBA1-260713-000001\nTestedAt,2026-07-13T08:00:00.000+08:00\n"
        "OverallResult,PASS\nTestPoint,TestName,LowerLimit,UpperLimit,Measured,Unit,Result\n"
        "TP01,RESISTANCE,1,2,1.5,ohm,PASS\n";
    ArchiveParseResult result =
        parseArchive(makeArchive(6, "TEST_REPORT", header), writeFile(header),
                     ParserProfile{"fct_csv_v1", 1}, 65536);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.failure_code, "FCT_CSV_METADATA_MISMATCH");
    EXPECT_TRUE(result.documents.empty());

    const std::string malformed =
        "ReportVersion,1.2\nDeviceId,ICT-TRI-01\nWorkOrder,WO-20260713-001\n"
        "ProductSN,CTRLMBA1-260713-000001\nTestedAt,2026-07-13T08:00:00.000+08:00\n"
        "OverallResult,PASS\nTestPoint,TestName,LowerLimit,UpperLimit,Measured,Unit,Result\n"
        "TP01,\"BROKEN,1,2,1.5,ohm,PASS\n";
    result = parseArchive(makeArchive(7, "TEST_REPORT", malformed), writeFile(malformed),
                          ParserProfile{"fct_csv_v1", 1}, 65536);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.failure_code, "FCT_CSV_FORMAT_INVALID");
    EXPECT_EQ(result.failure_line, 8U);
}

TEST_F(ArchiveParserTest, RejectsUnsupportedProfile) {
    const std::string content = "ignored\n";
    const ArchiveParseResult result =
        parseArchive(makeArchive(8, "RUNTIME_LOG", content), writeFile(content),
                     ParserProfile{"unknown_v1", 1}, 65536);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.failure_code, "PARSER_PROFILE_UNSUPPORTED");
}

}  // namespace
}  // namespace logtrace
}  // namespace smt
