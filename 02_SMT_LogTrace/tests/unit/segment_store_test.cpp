/**
 * @file segment_store_test.cpp
 * @brief 验证 PARSED 加载、Segment 格式、倒排数据、摘要拒绝和 pread 回读。
 */

#include "logtrace/indexing/segment_store.h"

#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "logtrace/common/sha256.h"
#include "logtrace/indexing/archive_parser.h"
#include "logtrace/indexing/index_snapshot.h"
#include "logtrace/indexing/parsed_batch_reader.h"
#include "logtrace/indexing/parsed_batch_writer.h"
#include "logtrace/storage/storage_paths.h"

namespace smt {
namespace logtrace {
namespace {

/// @brief 为 Segment 测试创建一期归档、解析工件和独立索引目录。
class SegmentStoreTest : public testing::Test {
   protected:
    /// @brief 创建临时归档与索引根目录。
    void SetUp() override {
        char path[] = "/tmp/logtrace-segment-test-XXXXXX";
        char* created = ::mkdtemp(path);
        ASSERT_NE(created, nullptr);
        root_ = created;
        archive_root_ = root_ + "/archive";
        index_root_ = root_ + "/index";
        ASSERT_EQ(::mkdir(archive_root_.c_str(), 0750), 0);
        const StorageConfig config{archive_root_, index_root_};
        storage_.reset(new StoragePaths(config));
        storage_->initialize();
    }

    /// @brief 删除测试创建的固定目录和文件。
    void TearDown() override {
        std::remove((archive_root_ + "/sample.log").c_str());
        std::remove((index_root_ + "/parsed/batch_7/manifest.json").c_str());
        std::remove((index_root_ + "/parsed/batch_7/archives.jsonl").c_str());
        std::remove((index_root_ + "/parsed/batch_7/documents.jsonl").c_str());
        std::remove((index_root_ + "/segments/segment_7/manifest.json").c_str());
        std::remove((index_root_ + "/segments/segment_7/files.bin").c_str());
        std::remove((index_root_ + "/segments/segment_7/documents.bin").c_str());
        std::remove((index_root_ + "/segments/segment_7/postings.bin").c_str());
        std::remove((index_root_ + "/segments/segment_7/terms.bin").c_str());
        ::rmdir((index_root_ + "/segments/segment_7").c_str());
        ::rmdir((index_root_ + "/segments/.building").c_str());
        ::rmdir((index_root_ + "/segments").c_str());
        ::rmdir((index_root_ + "/parsed/batch_7").c_str());
        ::rmdir((index_root_ + "/parsed/.building").c_str());
        ::rmdir((index_root_ + "/parsed").c_str());
        ::rmdir(index_root_.c_str());
        ::rmdir(archive_root_.c_str());
        ::rmdir(root_.c_str());
    }

    /// @brief 生成两行运行日志并完成 PARSED 和 Segment 构建。
    /// @return READY 描述和两条原始正文。
    std::pair<ReadySegmentDescriptor, std::vector<std::string> > buildSegment() {
        const std::string first =
            "2026-07-14T08:00:00.000+08:00 level=INFO module=inspection "
            "device=AOI-VT-01 station=ST-AOI-01 code=- result=PASS";
        const std::string second =
            "2026-07-14T08:00:01.000+08:00 level=ERROR module=inspection "
            "device=AOI-VT-01 station=ST-AOI-01 code=INSPECTION_NG result=NG";
        content_ = first + "\r\n" + second + "\n";
        std::ofstream output((archive_root_ + "/sample.log").c_str(), std::ios::binary);
        output.write(content_.data(), static_cast<std::streamsize>(content_.size()));
        output.close();

        Sha256 sha256;
        sha256.update(content_.data(), content_.size());
        ArchiveRecord archive;
        archive.archive_id = 10;
        archive.line_id = "LINE-01";
        archive.station_id = "ST-AOI-01";
        archive.device_id = "AOI-VT-01";
        archive.collector_id = "IPC-L01-01";
        archive.work_order = "WO-1";
        archive.product_sn = "SN-1";
        archive.file_type = "RUNTIME_LOG";
        archive.original_filename = "sample.log";
        archive.relative_path = "sample.log";
        archive.file_size = content_.size();
        archive.file_sha256 = sha256.finishHex();
        archive.produced_at = "2026-07-14T00:00:00.000Z";
        archive.archived_at = "2026-07-14T00:01:00.000Z";
        const ArchiveParseResult parsed = parseArchive(archive, archive_root_ + "/sample.log",
                                                       ParserProfile{"kv_runtime_v1", 1}, 65536);
        EXPECT_TRUE(parsed.success);
        EXPECT_EQ(parsed.documents.size(), 2U);

        const ParsedBatchWriter writer(index_root_);
        const ParsedBatchArtifact artifact = writer.write(
            7, 10, 10, 1, 0,
            {ParsedArchive{archive, ParserProfile{"kv_runtime_v1", 1}, parsed.documents}});
        const ParsedBatchDescriptor descriptor{
            7, 10, 10, 1, 2, artifact.relative_path, artifact.manifest_sha256};
        const ParsedBatchReader reader(index_root_);
        const ParsedBatchData batch = reader.load(descriptor);
        const SegmentStore store(*storage_);
        const SegmentBuildResult result = store.build(batch);
        return std::make_pair(
            ReadySegmentDescriptor{result.batch_id, result.segment_name, result.manifest_sha256},
            std::vector<std::string>{first, second});
    }

    std::string root_;
    std::string archive_root_;
    std::string index_root_;
    std::string content_;
    std::unique_ptr<StoragePaths> storage_;
};

TEST_F(SegmentStoreTest, BuildsInvertedDataAndReadsExactOriginalBytes) {
    const std::pair<ReadySegmentDescriptor, std::vector<std::string> > built = buildSegment();
    const SegmentStore store(*storage_);
    const LoadedSegment loaded = store.load(built.first);

    EXPECT_EQ(loaded.first_archive_id, 10U);
    EXPECT_EQ(loaded.last_archive_id, 10U);
    EXPECT_EQ(loaded.source_file_count, 1U);
    EXPECT_EQ(loaded.parsed_file_count, 1U);
    ASSERT_EQ(loaded.files.size(), 1U);
    ASSERT_EQ(loaded.documents.size(), 2U);
    EXPECT_NE(loaded.term_lookup.find("inspection"), loaded.term_lookup.end());
    EXPECT_NE(loaded.term_lookup.find("inspection_ng"), loaded.term_lookup.end());
    EXPECT_NE(loaded.term_lookup.find("aoi-vt-01"), loaded.term_lookup.end());
    const SegmentTermRecord& inspection = loaded.terms[loaded.term_lookup.at("inspection")];
    EXPECT_EQ(inspection.document_frequency, 2U);
    EXPECT_EQ(inspection.posting_count, 2U);

    std::vector<std::shared_ptr<const LoadedSegment> > segments;
    segments.push_back(std::shared_ptr<const LoadedSegment>(new LoadedSegment(loaded)));
    IndexSnapshotStore snapshots;
    snapshots.replace(std::shared_ptr<const IndexSnapshot>(new IndexSnapshot(segments)));
    EXPECT_EQ(snapshots.current()->version(), 7U);
    EXPECT_EQ(snapshots.current()->documentCount(), 2U);
    EXPECT_EQ(snapshots.readOriginal((7ULL << 32) | 0, *storage_), built.second[0]);
    EXPECT_EQ(snapshots.readOriginal((7ULL << 32) | 1, *storage_), built.second[1]);
    EXPECT_THROW(snapshots.readOriginal((8ULL << 32) | 0, *storage_), std::out_of_range);
}

TEST_F(SegmentStoreTest, RejectsCorruptedBinaryAndDoesNotStoreRawBody) {
    const std::pair<ReadySegmentDescriptor, std::vector<std::string> > built = buildSegment();
    const SegmentStore store(*storage_);
    const LoadedSegment loaded = store.load(built.first);
    std::vector<std::shared_ptr<const LoadedSegment> > segments;
    segments.push_back(std::shared_ptr<const LoadedSegment>(new LoadedSegment(loaded)));
    IndexSnapshotStore snapshots;
    snapshots.replace(std::shared_ptr<const IndexSnapshot>(new IndexSnapshot(segments)));
    const std::string documents_path = index_root_ + "/segments/segment_7/documents.bin";
    std::ifstream input(documents_path.c_str(), std::ios::binary);
    const std::string documents((std::istreambuf_iterator<char>(input)),
                                std::istreambuf_iterator<char>());
    EXPECT_EQ(documents.find(built.second[0]), std::string::npos);
    input.close();

    std::ofstream corrupt(documents_path.c_str(), std::ios::binary | std::ios::app);
    corrupt.put('x');
    corrupt.close();
    EXPECT_THROW(store.load(built.first), std::runtime_error);
    EXPECT_EQ(snapshots.current()->version(), 7U);
    EXPECT_EQ(snapshots.readOriginal((7ULL << 32) | 0, *storage_), built.second[0]);
}

TEST_F(SegmentStoreTest, RejectsCorruptedParsedChildBeforeBuilding) {
    buildSegment();
    const std::string documents_path = index_root_ + "/parsed/batch_7/documents.jsonl";
    std::ofstream corrupt(documents_path.c_str(), std::ios::binary | std::ios::app);
    corrupt << "{}\n";
    corrupt.close();
    std::ifstream manifest_input((index_root_ + "/parsed/batch_7/manifest.json").c_str());
    const std::string manifest((std::istreambuf_iterator<char>(manifest_input)),
                               std::istreambuf_iterator<char>());
    Sha256 sha256;
    sha256.update(manifest.data(), manifest.size());
    const ParsedBatchDescriptor descriptor{7, 10, 10, 1, 2, "parsed/batch_7", sha256.finishHex()};
    const ParsedBatchReader reader(index_root_);
    EXPECT_THROW(reader.load(descriptor), std::runtime_error);
}

}  // namespace
}  // namespace logtrace
}  // namespace smt
