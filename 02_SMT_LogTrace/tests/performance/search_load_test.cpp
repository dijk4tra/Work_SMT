/**
 * @file search_load_test.cpp
 * @brief 对固定百万文档快照执行并发 Top-K 检索负载验证。
 */

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "logtrace/search/search_engine.h"

namespace {

const std::size_t kDocumentCount = 1000000;
const std::size_t kThreadCount = 8;

smt::logtrace::SegmentDocumentRecord makeDocument(std::uint32_t local_id) {
    smt::logtrace::SegmentDocumentRecord document;
    document.doc_id = (1ULL << 32) | local_id;
    document.local_id = local_id;
    document.file_ordinal = 0;
    document.archive_id = 1;
    document.occurred_at_ms = local_id;
    document.archived_at_ms = local_id;
    document.term_count = 1;
    document.level = local_id % 100 == 0 ? "ERROR" : "INFO";
    return document;
}

}  // namespace

int main() {
    using namespace smt::logtrace;
    LoadedSegment* loaded = new LoadedSegment();
    loaded->batch_id = 1;
    loaded->documents.reserve(kDocumentCount);
    for (std::size_t index = 0; index < kDocumentCount; ++index)
        loaded->documents.push_back(makeDocument(static_cast<std::uint32_t>(index)));
    loaded->files.push_back(SegmentFileRecord{1, 0, std::string(64, 'a'), "load.log"});

    IndexSnapshotStore snapshots;
    snapshots.replace(std::shared_ptr<const IndexSnapshot>(
        new IndexSnapshot(std::vector<std::shared_ptr<const LoadedSegment> >{
            std::shared_ptr<const LoadedSegment>(loaded)})));
    const StoragePaths storage(StorageConfig{"/tmp", "/tmp/logtrace-load-index"});
    const CacheConfig cache{64, 192, 65536, 7200, 30, 10, 600, 300};
    const SearchEngine engine(snapshots, storage, cache);
    SearchQuery query;
    query.has_time_range = false;
    query.occurred_from_ms = 0;
    query.occurred_to_ms = 0;
    query.anomaly_only = true;
    query.offset = 0;
    query.page_size = 100;

    std::vector<SearchPage> pages(kThreadCount);
    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    std::vector<std::thread> workers;
    for (std::size_t index = 0; index < kThreadCount; ++index) {
        workers.push_back(std::thread(
            [index, &engine, &query, &pages]() { pages[index] = engine.search(query); }));
    }
    for (std::vector<std::thread>::iterator worker = workers.begin(); worker != workers.end();
         ++worker)
        worker->join();
    const std::int64_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now() - started)
                                        .count();

    for (std::size_t index = 0; index < pages.size(); ++index) {
        if (pages[index].total_hits != 10000 || pages[index].items.size() != 100 ||
            pages[index].items.front().document.local_id != 999900) {
            std::cerr << "load result is inconsistent" << std::endl;
            return 1;
        }
    }
    std::cout << "{\"documents\":" << kDocumentCount << ",\"threads\":" << kThreadCount
              << ",\"queries\":" << kThreadCount << ",\"elapsed_ms\":" << elapsed_ms << "}"
              << std::endl;
    return 0;
}
