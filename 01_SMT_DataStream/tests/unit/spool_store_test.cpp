/**
 * @file spool_store_test.cpp
 * @brief 验证 payload 快照和任务状态可在重启后恢复。
 */

#include "datastream/collector/spool_store.h"

#include <gtest/gtest.h>
#include <unistd.h>

#include <fstream>

namespace smt {
namespace datastream {
namespace {

TEST(SpoolStoreTest, PersistsSnapshotAndReloadsTask) {
    char root_template[] = "/tmp/collector-spool-XXXXXX";
    const char* root = ::mkdtemp(root_template);
    ASSERT_NE(root, nullptr);
    const std::string source = std::string(root) + "/source.bin";
    std::ofstream(source.c_str(), std::ios::binary) << "payload";
    SpoolStore store(std::string(root) + "/spool", 1024 * 1024, 0);
    EXPECT_TRUE(store.initialize().empty());
    CollectorTask task;
    task.task_id.assign(64, 'a');
    task.source_path = source;
    task.payload_path = store.snapshot(source, task.task_id, 7);
    task.line_id = "LINE-01";
    task.station_id = "ST-AOI-01";
    task.device_id = "AOI-VT-01";
    task.collector_id = "IPC-L01-01";
    task.file_type = "NG_IMAGE";
    task.original_filename = "source.bin";
    task.produced_at = "2026-07-12T00:00:00.000Z";
    task.file_size = 7;
    task.source_mtime_ns = 1;
    task.file_sha256.assign(64, 'b');
    task.state = CollectorTaskState::Uploading;
    task.upload_id = "123e4567-e89b-42d3-a456-426614174000";
    task.chunk_size = 1048576;
    task.chunk_count = 1;
    task.retry_attempts = 2;
    task.next_attempt_milliseconds = 10;
    task.last_error = "NETWORK_ERROR";
    task.archive_id = 0;
    store.save(task);
    const std::map<std::string, CollectorTask> loaded = store.initialize();
    ASSERT_EQ(loaded.size(), 1U);
    EXPECT_EQ(loaded.begin()->second.upload_id, task.upload_id);
    EXPECT_EQ(loaded.begin()->second.retry_attempts, 2);
    std::ifstream payload(task.payload_path.c_str(), std::ios::binary);
    std::string content;
    payload >> content;
    EXPECT_EQ(content, "payload");
    task.state = CollectorTaskState::Done;
    task.archive_id = 42;
    store.save(task);
    store.removePayload(task);
    EXPECT_EQ(::access(task.payload_path.c_str(), F_OK), -1);
    EXPECT_EQ(store.initialize().begin()->second.archive_id, 42U);
}

TEST(SpoolStoreTest, RemovesStrictInterruptedTemporaryFilesOnRestart) {
    char root_template[] = "/tmp/collector-spool-XXXXXX";
    const char* root = ::mkdtemp(root_template);
    ASSERT_NE(root, nullptr);
    const std::string spool_root = std::string(root) + "/spool";
    SpoolStore store(spool_root, 1024 * 1024, 0);
    EXPECT_TRUE(store.initialize().empty());
    const std::string task_id(64, 'c');
    const std::string state_temp = spool_root + "/states/" + task_id + ".json.tmp";
    const std::string file_temp = spool_root + "/files/" + task_id + ".data.tmp";
    std::ofstream(state_temp.c_str()) << "partial";
    std::ofstream(file_temp.c_str()) << "partial";
    EXPECT_TRUE(store.initialize().empty());
    EXPECT_EQ(::access(state_temp.c_str(), F_OK), -1);
    EXPECT_EQ(::access(file_temp.c_str(), F_OK), -1);
}

}  // namespace
}  // namespace datastream
}  // namespace smt
