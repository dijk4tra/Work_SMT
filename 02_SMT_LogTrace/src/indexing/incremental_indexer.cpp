/**
 * @file incremental_indexer.cpp
 * @brief 实现单批次增量扫描、文件级原子解析和状态迁移。
 */

#include "logtrace/indexing/incremental_indexer.h"

#include <fcntl.h>
#include <spdlog/spdlog.h>
#include <sys/file.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "logtrace/indexing/archive_parser.h"

namespace smt {
namespace logtrace {
namespace {

struct ArchiveOutcome {
    ArchiveRecord archive;
    bool profile_found;
    ParserProfile profile;
    ArchiveParseResult parse_result;
};

/// @brief 使用索引根目录文件锁串行化 Search Server 与管理命令。
class IndexOperationLock {
   public:
    explicit IndexOperationLock(const std::string& index_root)
        : fd_(::open((index_root + "/.indexer.lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0640)) {
        if (fd_ < 0) {
            throw std::runtime_error("cannot open index operation lock: " +
                                     std::string(std::strerror(errno)));
        }
        if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
            const int saved_errno = errno;
            ::close(fd_);
            fd_ = -1;
            if (saved_errno == EWOULDBLOCK) {
                throw std::runtime_error("another index operation is running");
            }
            throw std::runtime_error("cannot acquire index operation lock: " +
                                     std::string(std::strerror(saved_errno)));
        }
    }

    ~IndexOperationLock() {
        if (fd_ >= 0) {
            ::flock(fd_, LOCK_UN);
            ::close(fd_);
        }
    }

   private:
    int fd_;
};

ArchiveParseResult failedResult(const std::string& code, std::uint64_t line) {
    ArchiveParseResult result;
    result.success = false;
    result.failure_code = code;
    result.failure_line = line;
    return result;
}

}  // namespace

IncrementalIndexer::IncrementalIndexer(const IndexerDependencies& dependencies,
                                       const IndexingConfig& config)
    : storage_(dependencies.storage),
      config_(config),
      source_repository_(dependencies.source_mysql, dependencies.mysql_timeout_ms),
      state_repository_(dependencies.state_mysql, dependencies.mysql_timeout_ms),
      writer_(dependencies.storage.indexRoot()) {}

void IncrementalIndexer::recoverUnlocked() {
    std::vector<std::uint64_t> batch_ids;
    if (!state_repository_.recoverInterruptedBatches(&batch_ids)) {
        throw std::runtime_error("cannot recover interrupted parsing batches");
    }
    for (std::vector<std::uint64_t>::const_iterator it = batch_ids.begin(); it != batch_ids.end();
         ++it) {
        writer_.remove(*it);
        spdlog::warn("event=parsing_batch_recovered batch_id={} code=BATCH_INTERRUPTED", *it);
    }
}

void IncrementalIndexer::recover() {
    const IndexOperationLock lock(storage_.indexRoot());
    recoverUnlocked();
}

ScanSummary IncrementalIndexer::scanOnce() {
    const IndexOperationLock lock(storage_.indexRoot());
    recoverUnlocked();
    std::vector<std::uint64_t> pending_ids;
    if (!state_repository_.listPendingArchiveIds(config_.source_batch_limit, &pending_ids)) {
        throw std::runtime_error("cannot list pending archives");
    }

    std::vector<ArchiveRecord> source_archives;
    if (!pending_ids.empty()) {
        if (!source_repository_.findByIds(pending_ids, &source_archives)) {
            throw std::runtime_error("cannot read pending archives from source database");
        }
    } else {
        std::uint64_t after_archive_id = 0;
        if (!state_repository_.maxObservedArchiveId(&after_archive_id) ||
            !source_repository_.listAfter(after_archive_id, config_.source_batch_limit,
                                          &source_archives)) {
            throw std::runtime_error("cannot scan source archives");
        }
    }
    if (source_archives.empty()) {
        return ScanSummary{false, 0, 0, 0, 0, 0};
    }

    std::map<std::string, ParserProfile> profiles;
    if (!state_repository_.loadProfiles(&profiles)) {
        throw std::runtime_error("cannot load parser profiles");
    }

    std::vector<ArchiveOutcome> outcomes;
    std::size_t document_count = 0;
    for (std::vector<ArchiveRecord>::const_iterator archive = source_archives.begin();
         archive != source_archives.end(); ++archive) {
        ArchiveOutcome outcome;
        outcome.archive = *archive;
        const std::map<std::string, ParserProfile>::const_iterator profile =
            profiles.find(parserProfileKey(archive->device_id, archive->file_type));
        outcome.profile_found = profile != profiles.end();
        if (!outcome.profile_found) {
            outcome.parse_result = failedResult("PARSER_PROFILE_NOT_FOUND", 0);
            outcomes.push_back(outcome);
            continue;
        }
        outcome.profile = profile->second;
        try {
            const std::string path = storage_.resolveArchiveFile(archive->relative_path);
            outcome.parse_result =
                parseArchive(*archive, path, outcome.profile, config_.max_line_bytes);
        } catch (const ArchivePathError& error) {
            outcome.parse_result = failedResult(error.code(), 0);
        }

        if (outcome.parse_result.success &&
            outcome.parse_result.documents.size() > config_.document_batch_limit) {
            outcome.parse_result = failedResult("DOCUMENT_LIMIT_EXCEEDED", 0);
        } else if (outcome.parse_result.success && !outcomes.empty() &&
                   document_count + outcome.parse_result.documents.size() >
                       config_.document_batch_limit) {
            break;
        }
        if (outcome.parse_result.success) {
            document_count += outcome.parse_result.documents.size();
        }
        outcomes.push_back(outcome);
    }
    if (outcomes.empty()) {
        return ScanSummary{false, 0, 0, 0, 0, 0};
    }

    std::uint64_t batch_id = 0;
    if (!state_repository_.createBatch(outcomes.front().archive.archive_id,
                                       outcomes.back().archive.archive_id, outcomes.size(),
                                       &batch_id)) {
        throw std::runtime_error("cannot create parsing batch");
    }
    for (std::vector<ArchiveOutcome>::const_iterator outcome = outcomes.begin();
         outcome != outcomes.end(); ++outcome) {
        const ParserProfile* profile = outcome->profile_found ? &outcome->profile : nullptr;
        if (!state_repository_.attachArchive(batch_id, outcome->archive.archive_id, profile)) {
            throw std::runtime_error("cannot attach archive to parsing batch");
        }
    }

    std::vector<ParsedArchive> parsed_archives;
    std::size_t failed_count = 0;
    for (std::vector<ArchiveOutcome>::const_iterator outcome = outcomes.begin();
         outcome != outcomes.end(); ++outcome) {
        if (outcome->parse_result.success) {
            parsed_archives.push_back(
                ParsedArchive{outcome->archive, outcome->profile, outcome->parse_result.documents});
        } else {
            ++failed_count;
        }
    }

    if (parsed_archives.empty()) {
        for (std::vector<ArchiveOutcome>::const_iterator outcome = outcomes.begin();
             outcome != outcomes.end(); ++outcome) {
            if (!state_repository_.markArchiveFailed(outcome->archive.archive_id,
                                                     outcome->parse_result.failure_code,
                                                     outcome->parse_result.failure_line)) {
                throw std::runtime_error("cannot persist archive parsing failure");
            }
        }
        if (!state_repository_.markBatchFailed(batch_id, "NO_ARCHIVE_PARSED")) {
            throw std::runtime_error("cannot mark empty parsing batch failed");
        }
        return ScanSummary{true, batch_id, outcomes.size(), 0, failed_count, 0};
    }

    ParsedBatchArtifact artifact;
    try {
        artifact = writer_.write(batch_id, outcomes.front().archive.archive_id,
                                 outcomes.back().archive.archive_id, outcomes.size(), failed_count,
                                 parsed_archives);
    } catch (const std::exception&) {
        bool state_persisted = true;
        for (std::vector<ArchiveOutcome>::const_iterator outcome = outcomes.begin();
             outcome != outcomes.end(); ++outcome) {
            if (outcome->parse_result.success) {
                state_persisted =
                    state_repository_.markArchiveFailed(outcome->archive.archive_id,
                                                        "PARSED_ARTIFACT_WRITE_FAILED", 0) &&
                    state_persisted;
            } else {
                state_persisted =
                    state_repository_.markArchiveFailed(outcome->archive.archive_id,
                                                        outcome->parse_result.failure_code,
                                                        outcome->parse_result.failure_line) &&
                    state_persisted;
            }
        }
        state_persisted =
            state_repository_.markBatchFailed(batch_id, "PARSED_ARTIFACT_WRITE_FAILED") &&
            state_persisted;
        if (!state_persisted) {
            throw std::runtime_error("parsed artifact and failure state persistence both failed");
        }
        throw;
    }

    for (std::vector<ArchiveOutcome>::const_iterator outcome = outcomes.begin();
         outcome != outcomes.end(); ++outcome) {
        const bool stored =
            outcome->parse_result.success
                ? state_repository_.markArchiveParsed(outcome->archive.archive_id,
                                                      outcome->parse_result.documents.size())
                : state_repository_.markArchiveFailed(outcome->archive.archive_id,
                                                      outcome->parse_result.failure_code,
                                                      outcome->parse_result.failure_line);
        if (!stored) {
            throw std::runtime_error("cannot persist parsed archive state");
        }
    }
    if (!state_repository_.markBatchParsed(batch_id, artifact.relative_path,
                                           artifact.manifest_sha256, document_count)) {
        throw std::runtime_error("cannot publish parsed batch state");
    }

    spdlog::info(
        "event=parsing_batch_completed batch_id={} source_files={} parsed_files={} failed_files={} "
        "documents={}",
        batch_id, outcomes.size(), parsed_archives.size(), failed_count, document_count);
    return ScanSummary{true,         batch_id,      outcomes.size(), parsed_archives.size(),
                       failed_count, document_count};
}

RebuildStatus IncrementalIndexer::requestRebuild(std::uint64_t archive_id) {
    const IndexOperationLock lock(storage_.indexRoot());
    std::uint64_t batch_id = 0;
    const RebuildStatus status = state_repository_.requestRebuild(archive_id, &batch_id);
    if (status == RebuildStatus::Queued) {
        writer_.remove(batch_id);
        spdlog::info("event=parsing_batch_rebuild_queued archive_id={} batch_id={}", archive_id,
                     batch_id);
    }
    return status;
}

}  // namespace logtrace
}  // namespace smt
