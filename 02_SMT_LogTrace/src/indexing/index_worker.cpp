/**
 * @file index_worker.cpp
 * @brief 实现 Search Server 后台解析扫描和 Segment 发布循环。
 */

#include "logtrace/indexing/index_worker.h"

#include <spdlog/spdlog.h>

#include <cassert>
#include <chrono>
#include <exception>

namespace smt {
namespace logtrace {

IndexWorker::IndexWorker(IncrementalIndexer& indexer, SegmentManager& segment_manager,
                         int poll_interval_ms)
    : indexer_(indexer),
      segment_manager_(segment_manager),
      poll_interval_ms_(poll_interval_ms),
      running_(false),
      stopping_(false) {}

IndexWorker::~IndexWorker() {
    if (running_) {
        stop();
    }
}

void IndexWorker::start() {
    assert(!running_);
    stopping_ = false;
    thread_ = std::thread(&IndexWorker::run, this);
    running_ = true;
}

void IndexWorker::stop() {
    assert(running_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    condition_.notify_all();
    thread_.join();
    running_ = false;
}

void IndexWorker::run() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopping_) {
        if (condition_.wait_for(lock, std::chrono::milliseconds(poll_interval_ms_),
                                [this]() { return stopping_; })) {
            break;
        }
        lock.unlock();
        try {
            indexer_.scanOnce();
            segment_manager_.buildNext();
        } catch (const std::exception& error) {
            spdlog::error("event=background_index_failed reason={}", error.what());
        }
        lock.lock();
    }
}

}  // namespace logtrace
}  // namespace smt
