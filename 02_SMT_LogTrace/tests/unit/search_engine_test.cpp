/**
 * @file search_engine_test.cpp
 * @brief 验证 AND、BM25、业务权重、Top-K、过滤和异常查询。
 */

#include "logtrace/search/search_engine.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace smt {
namespace logtrace {
namespace {

SegmentDocumentRecord document(std::uint32_t local_id, std::uint32_t term_count,
                               const std::string& level, const std::string& error_code,
                               std::int64_t occurred_at) {
    SegmentDocumentRecord value;
    value.doc_id = (1ULL << 32) | local_id;
    value.local_id = local_id;
    value.file_ordinal = 0;
    value.archive_id = 10;
    value.byte_offset = local_id * 10;
    value.byte_length = 5;
    value.occurred_at_ms = occurred_at;
    value.archived_at_ms = occurred_at + 1000;
    value.term_count = term_count;
    value.line_id = "LINE-01";
    value.station_id = "ST-AOI-01";
    value.device_id = "AOI-VT-01";
    value.collector_id = "IPC-01";
    value.source_type = "RUNTIME_LOG";
    value.level = level;
    value.module_name = "inspection";
    value.error_code = error_code;
    value.event_name = "inspection_result";
    return value;
}

std::shared_ptr<const LoadedSegment> makeSegment() {
    LoadedSegment* segment = new LoadedSegment();
    segment->batch_id = 1;
    segment->documents.push_back(document(0, 2, "INFO", "", 1000));
    segment->documents.push_back(document(1, 3, "INFO", "", 2000));
    segment->documents.push_back(document(2, 2, "ERROR", "INSPECTION_NG", 3000));
    segment->files.push_back(SegmentFileRecord{10, 100, std::string(64, 'a'), "sample.log"});
    segment->postings.push_back(SegmentPosting{0, 1});
    segment->postings.push_back(SegmentPosting{1, 1});
    segment->postings.push_back(SegmentPosting{2, 1});
    segment->postings.push_back(SegmentPosting{2, 1});
    segment->postings.push_back(SegmentPosting{0, 1});
    segment->postings.push_back(SegmentPosting{1, 2});
    segment->terms.push_back(SegmentTermRecord{"camera", 3, 0, 3});
    segment->terms.push_back(SegmentTermRecord{"ng", 1, 3, 1});
    segment->terms.push_back(SegmentTermRecord{"timeout", 2, 4, 2});
    segment->term_lookup["camera"] = 0;
    segment->term_lookup["ng"] = 1;
    segment->term_lookup["timeout"] = 2;
    return std::shared_ptr<const LoadedSegment>(segment);
}

SearchQuery baseQuery() {
    SearchQuery query;
    query.has_time_range = false;
    query.occurred_from_ms = 0;
    query.occurred_to_ms = 0;
    query.anomaly_only = false;
    query.offset = 0;
    query.page_size = 10;
    return query;
}

TEST(SearchEngineTest, AppliesAndBm25AndStableTopKPagination) {
    std::vector<std::shared_ptr<const LoadedSegment> > segments(1, makeSegment());
    IndexSnapshotStore snapshots;
    snapshots.replace(std::shared_ptr<const IndexSnapshot>(new IndexSnapshot(segments)));
    const StoragePaths storage(StorageConfig{"/tmp", "/tmp/logtrace-search-test-index"});
    const SearchEngine engine(snapshots, storage);

    SearchQuery query = baseQuery();
    query.keywords.push_back("CAMERA");
    query.keywords.push_back("timeout");
    query.page_size = 1;
    const SearchPage first = engine.search(query);
    ASSERT_EQ(first.total_hits, 2U);
    ASSERT_EQ(first.items.size(), 1U);
    EXPECT_EQ(first.items[0].document.local_id, 1U);

    query.offset = 1;
    const SearchPage second = engine.search(query);
    ASSERT_EQ(second.items.size(), 1U);
    EXPECT_EQ(second.items[0].document.local_id, 0U);
}

TEST(SearchEngineTest, AppliesBusinessWeightsFiltersAndAnomalyRule) {
    std::vector<std::shared_ptr<const LoadedSegment> > segments(1, makeSegment());
    IndexSnapshotStore snapshots;
    snapshots.replace(std::shared_ptr<const IndexSnapshot>(new IndexSnapshot(segments)));
    const StoragePaths storage(StorageConfig{"/tmp", "/tmp/logtrace-search-test-index"});
    const SearchEngine engine(snapshots, storage);

    SearchQuery query = baseQuery();
    query.anomaly_only = true;
    const SearchPage anomalies = engine.search(query);
    ASSERT_EQ(anomalies.total_hits, 1U);
    EXPECT_EQ(anomalies.items[0].document.error_code, "INSPECTION_NG");

    query.anomaly_only = false;
    query.error_code = "INSPECTION_NG";
    const SearchPage exact = engine.search(query);
    ASSERT_EQ(exact.total_hits, 1U);
    EXPECT_GT(exact.items[0].score, 4.0);
}

}  // namespace
}  // namespace logtrace
}  // namespace smt
