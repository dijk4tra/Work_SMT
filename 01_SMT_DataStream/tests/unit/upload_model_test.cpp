/**
 * @file upload_model_test.cpp
 * @brief 验证创建上传请求和 Redis 会话的严格解析。
 */

#include "datastream/upload/upload_model.h"

#include <gtest/gtest.h>

namespace smt {
namespace datastream {
namespace {

UploadConfig uploadConfig() {
    UploadConfig config;
    config.min_chunk_size_bytes = 1024;
    config.max_chunk_size_bytes = 8192;
    config.max_file_size_bytes = 1024 * 1024;
    return config;
}

std::string validRequest() {
    return R"({"station_id":"ST-AOI-01","collector_id":"IPC-L01-01",)"
           R"("work_order":"WO-1","product_sn":"SN-1","file_type":"NG_IMAGE",)"
           R"("result":"NG","original_filename":"board.png","file_size":9000,)"
           R"("file_sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",)"
           R"("chunk_size":4096,"produced_at":"2026-07-11T08:00:00.000+08:00"})";
}

TEST(UploadModelTest, ParsesValidNgImageAndComputesChunkCount) {
    CreateUploadRequest request;
    std::string error;
    ASSERT_TRUE(parseCreateUploadRequest(validRequest(), uploadConfig(), &request, &error));
    EXPECT_EQ(request.chunk_count, 3U);
    EXPECT_EQ(request.extension, "png");
}

TEST(UploadModelTest, RejectsUnknownFieldAndIncompleteTraceMetadata) {
    CreateUploadRequest request;
    std::string error;
    std::string unknown = validRequest();
    unknown.insert(unknown.size() - 1, ",\"extra\":1");
    EXPECT_FALSE(parseCreateUploadRequest(unknown, uploadConfig(), &request, &error));

    std::string missing_sn = validRequest();
    const std::string from = "\"product_sn\":\"SN-1\"";
    missing_sn.replace(missing_sn.find(from), from.size(), "\"product_sn\":null");
    EXPECT_FALSE(parseCreateUploadRequest(missing_sn, uploadConfig(), &request, &error));
}

TEST(UploadModelTest, RejectsUnsafeFilenameAndChunkSize) {
    CreateUploadRequest request;
    std::string error;
    std::string unsafe = validRequest();
    const std::string from = "board.png";
    unsafe.replace(unsafe.find(from), from.size(), "../board.png");
    EXPECT_FALSE(parseCreateUploadRequest(unsafe, uploadConfig(), &request, &error));

    std::string small_chunk = validRequest();
    small_chunk.replace(small_chunk.find("4096"), 4, "512");
    EXPECT_FALSE(parseCreateUploadRequest(small_chunk, uploadConfig(), &request, &error));
}

TEST(UploadModelTest, ParsesCompleteRedisSession) {
    const std::vector<std::string> fields{"upload_id",
                                          "id",
                                          "state",
                                          "UPLOADING",
                                          "device_id",
                                          "AOI-VT-01",
                                          "station_id",
                                          "ST-AOI-01",
                                          "line_id",
                                          "LINE-01",
                                          "collector_id",
                                          "IPC-L01-01",
                                          "file_type",
                                          "NG_IMAGE",
                                          "original_filename",
                                          "board.png",
                                          "temp_path",
                                          "/tmp/a",
                                          "file_size",
                                          "9000",
                                          "file_sha256",
                                          std::string(64, 'a'),
                                          "chunk_size",
                                          "4096",
                                          "chunk_count",
                                          "3",
                                          "expires_at",
                                          "1780000000",
                                          "failure_code",
                                          ""};
    UploadSession session;
    ASSERT_TRUE(parseUploadSession(fields, &session));
    EXPECT_EQ(session.chunk_count, 3U);
    EXPECT_EQ(session.file_size, 9000U);
}

}  // namespace
}  // namespace datastream
}  // namespace smt
