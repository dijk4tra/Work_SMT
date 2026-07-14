/**
 * @file admin_main.cpp
 * @brief LogTrace 单次扫描和明确重建命令入口。
 */

#include <cstdint>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

#include "logtrace/config/app_config.h"
#include "logtrace/indexing/incremental_indexer.h"
#include "logtrace/indexing/index_snapshot.h"
#include "logtrace/indexing/segment_manager.h"
#include "logtrace/storage/mysql_client.h"
#include "logtrace/storage/storage_paths.h"

namespace {

/// @brief 已完成命令行校验的管理命令。
struct AdminCommand {
    std::string config_path;
    std::string action;
    std::uint64_t archive_id;
};

AdminCommand parseCommand(int argc, char* argv[]) {
    if (argc == 4 && std::string(argv[1]) == "--config" && argv[2][0] != '\0') {
        const std::string action = argv[3];
        if (action == "scan-once" || action == "build-once") {
            return AdminCommand{argv[2], action, 0};
        }
    }
    if (argc == 6 && std::string(argv[1]) == "--config" && argv[2][0] != '\0' &&
        std::string(argv[3]) == "rebuild" && std::string(argv[4]) == "--archive-id") {
        std::size_t consumed = 0;
        const std::uint64_t archive_id = std::stoull(argv[5], &consumed);
        if (archive_id == 0 || consumed != std::string(argv[5]).size()) {
            throw std::invalid_argument("archive_id must be a positive integer");
        }
        return AdminCommand{argv[2], "rebuild", archive_id};
    }
    throw std::invalid_argument(
        "usage: logtrace_admin --config <path> "
        "<scan-once|build-once|rebuild --archive-id ID>");
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        const AdminCommand command = parseCommand(argc, argv);
        const smt::logtrace::AppConfig config = smt::logtrace::AppConfig::load(command.config_path);
        smt::logtrace::StoragePaths storage(config.storage);
        storage.initialize();
        smt::logtrace::MySqlClient source_mysql(config.source_mysql);
        smt::logtrace::MySqlClient state_mysql(config.state_mysql);
        if (!source_mysql.ping(config.health.check_timeout_ms) ||
            !state_mysql.ping(config.health.check_timeout_ms)) {
            throw std::runtime_error("MySQL startup probe failed");
        }
        smt::logtrace::IncrementalIndexer indexer(
            smt::logtrace::IndexerDependencies{source_mysql, state_mysql, storage,
                                               config.health.check_timeout_ms},
            config.indexing);
        if (command.action == "scan-once") {
            const smt::logtrace::ScanSummary summary = indexer.scanOnce();
            std::cout << nlohmann::json{{"batch_created", summary.batch_created},
                                        {"batch_id", summary.batch_id},
                                        {"source_file_count", summary.source_file_count},
                                        {"parsed_file_count", summary.parsed_file_count},
                                        {"failed_file_count", summary.failed_file_count},
                                        {"document_count", summary.document_count}}
                             .dump()
                      << '\n';
            return 0;
        }
        if (command.action == "build-once") {
            smt::logtrace::IndexSnapshotStore snapshots;
            smt::logtrace::SegmentManager manager(state_mysql, storage,
                                                  config.health.check_timeout_ms, snapshots);
            manager.recoverAndLoad();
            const smt::logtrace::SegmentBuildSummary summary = manager.buildNext();
            const std::shared_ptr<const smt::logtrace::IndexSnapshot> snapshot =
                snapshots.current();
            std::cout << nlohmann::json{{"batch_built", summary.batch_built},
                                        {"batch_id", summary.result.batch_id},
                                        {"segment_name", summary.result.segment_name},
                                        {"document_count", summary.result.document_count},
                                        {"term_count", summary.result.term_count},
                                        {"posting_count", summary.result.posting_count},
                                        {"snapshot_version", snapshot->version()},
                                        {"snapshot_segment_count", snapshot->segmentCount()},
                                        {"snapshot_document_count", snapshot->documentCount()}}
                             .dump()
                      << '\n';
            return 0;
        }

        const smt::logtrace::RebuildStatus status = indexer.requestRebuild(command.archive_id);
        if (status == smt::logtrace::RebuildStatus::NotFound) {
            std::cerr << "logtrace_admin: archive has no parsing state\n";
            return 2;
        }
        if (status == smt::logtrace::RebuildStatus::Unavailable) {
            throw std::runtime_error("cannot queue archive rebuild");
        }
        std::cout << nlohmann::json{{"status", "queued"}, {"archive_id", command.archive_id}}.dump()
                  << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "logtrace_admin: " << error.what() << '\n';
        return 1;
    }
}
