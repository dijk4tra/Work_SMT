/**
 * @file segment_manager.cpp
 * @brief 实现 BUILDING 恢复、READY 原子发布和查询快照交换。
 */

#include "logtrace/indexing/segment_manager.h"

#include <spdlog/spdlog.h>

#include <exception>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace smt {
namespace logtrace {
namespace {

std::string expectedSegmentName(std::uint64_t batch_id) {
    return "segment_" + std::to_string(batch_id);
}

}  // namespace

SegmentManager::SegmentManager(const MySqlClient& state_mysql, const StoragePaths& storage,
                               int timeout_ms, IndexSnapshotStore& snapshots)
    : state_repository_(state_mysql, timeout_ms),
      parsed_reader_(storage.indexRoot()),
      segment_store_(storage),
      snapshots_(snapshots) {}

void SegmentManager::recoverBuildingBatches() {
    std::vector<ParsedBatchDescriptor> batches;
    if (!state_repository_.listBuildingBatches(&batches)) {
        throw std::runtime_error("cannot list interrupted BUILDING batches");
    }
    for (std::vector<ParsedBatchDescriptor>::const_iterator batch = batches.begin();
         batch != batches.end(); ++batch) {
        if (!segment_store_.publishedExists(batch->batch_id)) {
            if (!state_repository_.resetBuildingBatch(batch->batch_id,
                                                      "SEGMENT_BUILD_INTERRUPTED")) {
                throw std::runtime_error("cannot reset interrupted BUILDING batch");
            }
            spdlog::warn("event=segment_build_reset batch_id={} code=SEGMENT_BUILD_INTERRUPTED",
                         batch->batch_id);
            continue;
        }
        LoadedSegment segment;
        try {
            segment = segment_store_.loadUnpublished(batch->batch_id);
        } catch (const std::exception& error) {
            segment_store_.remove(batch->batch_id);
            if (!state_repository_.markSegmentBuildFailed(batch->batch_id,
                                                          "SEGMENT_RECOVERY_CORRUPTED")) {
                throw std::runtime_error("cannot persist corrupted Segment recovery state");
            }
            spdlog::error("event=segment_recovery_failed batch_id={} reason={}", batch->batch_id,
                          error.what());
            continue;
        }
        if (!state_repository_.publishBatchReady(batch->batch_id, segment.segment_name,
                                                 segment.manifest_sha256)) {
            throw std::runtime_error("cannot publish recovered Segment");
        }
        spdlog::info("event=segment_recovered_ready batch_id={} segment={}", batch->batch_id,
                     segment.segment_name);
    }
}

void SegmentManager::recoverAndLoad() {
    segment_store_.cleanupBuilding();
    recoverBuildingBatches();
    refreshSnapshot();

    std::vector<ReadySegmentDescriptor> ready;
    if (!state_repository_.listReadySegments(&ready)) {
        throw std::runtime_error("cannot list READY segments for orphan audit");
    }
    std::set<std::string> ready_names;
    for (std::vector<ReadySegmentDescriptor>::const_iterator segment = ready.begin();
         segment != ready.end(); ++segment) {
        ready_names.insert(segment->segment_name);
    }
    const std::vector<std::string> published = segment_store_.listPublishedNames();
    for (std::vector<std::string>::const_iterator name = published.begin(); name != published.end();
         ++name) {
        if (ready_names.count(*name) == 0) {
            spdlog::warn("event=orphan_segment_ignored segment={}", *name);
        }
    }
}

SegmentBuildSummary SegmentManager::buildNext() {
    recoverBuildingBatches();
    std::vector<ParsedBatchDescriptor> batches;
    if (!state_repository_.listParsedBatches(1, &batches)) {
        throw std::runtime_error("cannot list PARSED batches");
    }
    if (batches.empty()) {
        return SegmentBuildSummary{false, SegmentBuildResult{0, "", "", 0, 0, 0}};
    }
    const ParsedBatchDescriptor& descriptor = batches.front();
    const std::string name = expectedSegmentName(descriptor.batch_id);
    if (!state_repository_.markBatchBuilding(descriptor.batch_id, name)) {
        throw std::runtime_error("cannot mark PARSED batch BUILDING");
    }

    bool renamed = false;
    SegmentBuildResult result;
    try {
        const ParsedBatchData batch = parsed_reader_.load(descriptor);
        result = segment_store_.build(batch);
        renamed = true;
        if (!state_repository_.publishBatchReady(result.batch_id, result.segment_name,
                                                 result.manifest_sha256)) {
            throw std::runtime_error("cannot atomically publish READY batch");
        }
    } catch (const std::exception&) {
        if (!renamed) {
            segment_store_.remove(descriptor.batch_id);
            if (!state_repository_.markSegmentBuildFailed(descriptor.batch_id,
                                                          "SEGMENT_BUILD_FAILED")) {
                throw std::runtime_error("Segment build and failure state persistence both failed");
            }
        }
        throw;
    }

    refreshSnapshot();
    spdlog::info("event=segment_ready batch_id={} segment={} documents={} terms={} postings={}",
                 result.batch_id, result.segment_name, result.document_count, result.term_count,
                 result.posting_count);
    return SegmentBuildSummary{true, result};
}

void SegmentManager::refreshSnapshot() {
    std::vector<ReadySegmentDescriptor> descriptors;
    if (!state_repository_.listReadySegments(&descriptors)) {
        throw std::runtime_error("cannot list READY segments");
    }
    std::vector<std::shared_ptr<const LoadedSegment> > segments;
    for (std::vector<ReadySegmentDescriptor>::const_iterator descriptor = descriptors.begin();
         descriptor != descriptors.end(); ++descriptor) {
        segments.push_back(std::shared_ptr<const LoadedSegment>(
            new LoadedSegment(segment_store_.load(*descriptor))));
    }
    snapshots_.replace(std::shared_ptr<const IndexSnapshot>(new IndexSnapshot(segments)));
}

}  // namespace logtrace
}  // namespace smt
