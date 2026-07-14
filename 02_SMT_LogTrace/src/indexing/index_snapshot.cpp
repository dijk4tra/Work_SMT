/**
 * @file index_snapshot.cpp
 * @brief 实现不可变 READY 快照交换、文档定位和原文回读。
 */

#include "logtrace/indexing/index_snapshot.h"

#include <algorithm>
#include <stdexcept>

#include "logtrace/indexing/original_log_reader.h"

namespace smt {
namespace logtrace {

IndexSnapshot::IndexSnapshot() : version_(0), document_count_(0) {}

IndexSnapshot::IndexSnapshot(const std::vector<std::shared_ptr<const LoadedSegment> >& segments)
    : segments_(segments), version_(0), document_count_(0) {
    std::uint64_t previous = 0;
    for (std::vector<std::shared_ptr<const LoadedSegment> >::const_iterator segment =
             segments_.begin();
         segment != segments_.end(); ++segment) {
        if (!*segment || (*segment)->batch_id <= previous) {
            throw std::invalid_argument("snapshot segments must have increasing batch ids");
        }
        previous = (*segment)->batch_id;
        version_ = (*segment)->batch_id;
        document_count_ += (*segment)->documents.size();
    }
}

std::uint64_t IndexSnapshot::version() const { return version_; }

std::size_t IndexSnapshot::segmentCount() const { return segments_.size(); }

std::size_t IndexSnapshot::documentCount() const { return document_count_; }

bool IndexSnapshot::findDocument(std::uint64_t doc_id, const SegmentDocumentRecord** document,
                                 const SegmentFileRecord** file) const {
    const std::uint64_t batch_id = doc_id >> 32;
    const std::uint32_t local_id = static_cast<std::uint32_t>(doc_id & 0xFFFFFFFFULL);
    for (std::vector<std::shared_ptr<const LoadedSegment> >::const_iterator segment =
             segments_.begin();
         segment != segments_.end(); ++segment) {
        if ((*segment)->batch_id < batch_id) {
            continue;
        }
        if ((*segment)->batch_id > batch_id || local_id >= (*segment)->documents.size()) {
            return false;
        }
        const SegmentDocumentRecord& candidate = (*segment)->documents[local_id];
        if (candidate.doc_id != doc_id || candidate.file_ordinal >= (*segment)->files.size()) {
            return false;
        }
        *document = &candidate;
        *file = &(*segment)->files[candidate.file_ordinal];
        return true;
    }
    return false;
}

IndexSnapshotStore::IndexSnapshotStore()
    : snapshot_(std::shared_ptr<const IndexSnapshot>(new IndexSnapshot())) {}

void IndexSnapshotStore::replace(const std::shared_ptr<const IndexSnapshot>& snapshot) {
    if (!snapshot) {
        throw std::invalid_argument("snapshot cannot be null");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_ = snapshot;
}

std::shared_ptr<const IndexSnapshot> IndexSnapshotStore::current() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

std::string IndexSnapshotStore::readOriginal(std::uint64_t doc_id,
                                             const StoragePaths& storage) const {
    const std::shared_ptr<const IndexSnapshot> snapshot = current();
    const SegmentDocumentRecord* document = nullptr;
    const SegmentFileRecord* file = nullptr;
    if (!snapshot->findDocument(doc_id, &document, &file)) {
        throw std::out_of_range("document is not present in current READY snapshot");
    }
    return readOriginalRecord(storage, *file, document->byte_offset, document->byte_length);
}

}  // namespace logtrace
}  // namespace smt
