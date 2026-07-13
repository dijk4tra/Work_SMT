/**
 * @file index_state_repository.cpp
 * @brief 实现解析配置、批次和归档消费状态持久化。
 */

#include "logtrace/indexing/index_state_repository.h"

#include <workflow/MySQLResult.h>
#include <workflow/MySQLUtil.h>
#include <workflow/WFFacilities.h>

#include <atomic>
#include <string>
#include <vector>

namespace smt {
namespace logtrace {
namespace {

std::string quoteSql(const std::string& value) {
    return "'" + protocol::MySQLUtil::escape_string_quote(value, '\'') + "'";
}

bool executeUpdate(const MySqlClient& mysql, int timeout_ms, const std::string& sql) {
    std::atomic<bool> success(false);
    WFFacilities::WaitGroup wait_group(1);
    WFMySQLTask* task =
        mysql.createQuery(sql, timeout_ms, [&success, &wait_group](WFMySQLTask* completed) {
            success.store(completed->get_state() == WFT_STATE_SUCCESS &&
                              !completed->get_resp()->is_error_packet(),
                          std::memory_order_release);
            wait_group.done();
        });
    task->start();
    wait_group.wait();
    return success.load(std::memory_order_acquire);
}

bool queryUnsignedValues(const MySqlClient& mysql, int timeout_ms, const std::string& sql,
                         std::vector<std::uint64_t>* values) {
    values->clear();
    std::atomic<bool> success(false);
    WFFacilities::WaitGroup wait_group(1);
    WFMySQLTask* task =
        mysql.createQuery(sql, timeout_ms, [&success, &wait_group, values](WFMySQLTask* completed) {
            if (completed->get_state() != WFT_STATE_SUCCESS ||
                completed->get_resp()->is_error_packet()) {
                wait_group.done();
                return;
            }
            protocol::MySQLResultCursor cursor(completed->get_resp());
            std::vector<protocol::MySQLCell> row;
            while (cursor.fetch_row(row)) {
                if (row.size() != 1 || row[0].is_null()) {
                    values->clear();
                    wait_group.done();
                    return;
                }
                values->push_back(row[0].as_ulonglong());
            }
            success.store(true, std::memory_order_release);
            wait_group.done();
        });
    task->start();
    wait_group.wait();
    return success.load(std::memory_order_acquire);
}

}  // namespace

std::string parserProfileKey(const std::string& device_id, const std::string& file_type) {
    return device_id + "\n" + file_type;
}

IndexStateRepository::IndexStateRepository(const MySqlClient& mysql, int timeout_ms)
    : mysql_(mysql), timeout_ms_(timeout_ms) {}

bool IndexStateRepository::loadProfiles(std::map<std::string, ParserProfile>* profiles) const {
    profiles->clear();
    std::atomic<bool> success(false);
    WFFacilities::WaitGroup wait_group(1);
    WFMySQLTask* task = mysql_.createQuery(
        "SELECT device_id,file_type,profile_name FROM parser_profile WHERE enabled=1", timeout_ms_,
        [&success, &wait_group, profiles](WFMySQLTask* completed) {
            if (completed->get_state() != WFT_STATE_SUCCESS ||
                completed->get_resp()->is_error_packet()) {
                wait_group.done();
                return;
            }
            protocol::MySQLResultCursor cursor(completed->get_resp());
            std::vector<protocol::MySQLCell> row;
            while (cursor.fetch_row(row)) {
                if (row.size() != 3 || row[0].is_null() || row[1].is_null() || row[2].is_null()) {
                    profiles->clear();
                    wait_group.done();
                    return;
                }
                (*profiles)[parserProfileKey(row[0].as_string(), row[1].as_string())] =
                    ParserProfile{row[2].as_string(), 1};
            }
            success.store(true, std::memory_order_release);
            wait_group.done();
        });
    task->start();
    wait_group.wait();
    return success.load(std::memory_order_acquire);
}

bool IndexStateRepository::listPendingArchiveIds(std::size_t limit,
                                                 std::vector<std::uint64_t>* archive_ids) const {
    return queryUnsignedValues(mysql_, timeout_ms_,
                               "SELECT archive_id FROM indexed_archive WHERE state='PENDING' "
                               "ORDER BY archive_id ASC LIMIT " +
                                   std::to_string(limit),
                               archive_ids);
}

bool IndexStateRepository::maxObservedArchiveId(std::uint64_t* archive_id) const {
    std::atomic<bool> success(false);
    WFFacilities::WaitGroup wait_group(1);
    WFMySQLTask* task =
        mysql_.createQuery("SELECT MAX(archive_id) FROM indexed_archive", timeout_ms_,
                           [&success, &wait_group, archive_id](WFMySQLTask* completed) {
                               if (completed->get_state() != WFT_STATE_SUCCESS ||
                                   completed->get_resp()->is_error_packet()) {
                                   wait_group.done();
                                   return;
                               }
                               protocol::MySQLResultCursor cursor(completed->get_resp());
                               std::vector<protocol::MySQLCell> row;
                               if (!cursor.fetch_row(row) || row.size() != 1) {
                                   wait_group.done();
                                   return;
                               }
                               *archive_id = row[0].is_null() ? 0 : row[0].as_ulonglong();
                               success.store(true, std::memory_order_release);
                               wait_group.done();
                           });
    task->start();
    wait_group.wait();
    return success.load(std::memory_order_acquire);
}

bool IndexStateRepository::createBatch(std::uint64_t first_archive_id,
                                       std::uint64_t last_archive_id, std::size_t source_file_count,
                                       std::uint64_t* batch_id) const {
    const std::string sql =
        "INSERT INTO index_batch(first_archive_id,last_archive_id,state,source_file_count) "
        "VALUES(" +
        std::to_string(first_archive_id) + "," + std::to_string(last_archive_id) + ",'PARSING'," +
        std::to_string(source_file_count) + ")";
    std::atomic<bool> success(false);
    WFFacilities::WaitGroup wait_group(1);
    WFMySQLTask* task = mysql_.createQuery(
        sql, timeout_ms_, [&success, &wait_group, batch_id](WFMySQLTask* completed) {
            if (completed->get_state() == WFT_STATE_SUCCESS &&
                !completed->get_resp()->is_error_packet()) {
                protocol::MySQLResultCursor cursor(completed->get_resp());
                *batch_id = cursor.get_insert_id();
                success.store(*batch_id != 0, std::memory_order_release);
            }
            wait_group.done();
        });
    task->start();
    wait_group.wait();
    return success.load(std::memory_order_acquire);
}

bool IndexStateRepository::attachArchive(std::uint64_t batch_id, std::uint64_t archive_id,
                                         const ParserProfile* profile) const {
    const std::string profile_name = profile == nullptr ? "NULL" : quoteSql(profile->name);
    const std::string profile_version =
        profile == nullptr ? "NULL" : std::to_string(profile->version);
    const std::string sql =
        "INSERT INTO indexed_archive(archive_id,batch_id,parser_profile,parser_version,state,"
        "document_count,failure_code,failure_line,indexed_at) VALUES(" +
        std::to_string(archive_id) + "," + std::to_string(batch_id) + "," + profile_name + "," +
        profile_version + ",'PENDING',0,NULL,NULL,NULL) ON DUPLICATE KEY UPDATE batch_id=" +
        std::to_string(batch_id) + ",parser_profile=" + profile_name +
        ",parser_version=" + profile_version +
        ",state='PENDING',document_count=0,failure_code=NULL,"
        "failure_line=NULL,indexed_at=NULL";
    return executeUpdate(mysql_, timeout_ms_, sql);
}

bool IndexStateRepository::markArchiveParsed(std::uint64_t archive_id,
                                             std::size_t document_count) const {
    return executeUpdate(mysql_, timeout_ms_,
                         "UPDATE indexed_archive SET state='PARSED',document_count=" +
                             std::to_string(document_count) +
                             ",failure_code=NULL,failure_line=NULL,indexed_at=UTC_TIMESTAMP(3) "
                             "WHERE archive_id=" +
                             std::to_string(archive_id));
}

bool IndexStateRepository::markArchiveFailed(std::uint64_t archive_id,
                                             const std::string& failure_code,
                                             std::uint64_t failure_line) const {
    const std::string line = failure_line == 0 ? "NULL" : std::to_string(failure_line);
    return executeUpdate(
        mysql_, timeout_ms_,
        "UPDATE indexed_archive SET state='FAILED',document_count=0,failure_code=" +
            quoteSql(failure_code) + ",failure_line=" + line +
            ",indexed_at=NULL WHERE archive_id=" + std::to_string(archive_id));
}

bool IndexStateRepository::markBatchParsed(std::uint64_t batch_id, const std::string& parsed_path,
                                           const std::string& parsed_sha256,
                                           std::size_t document_count) const {
    return executeUpdate(
        mysql_, timeout_ms_,
        "UPDATE index_batch SET state='PARSED',document_count=" + std::to_string(document_count) +
            ",parsed_path=" + quoteSql(parsed_path) + ",parsed_sha256=UNHEX(" +
            quoteSql(parsed_sha256) +
            "),failure_code=NULL WHERE batch_id=" + std::to_string(batch_id));
}

bool IndexStateRepository::markBatchFailed(std::uint64_t batch_id,
                                           const std::string& failure_code) const {
    return executeUpdate(
        mysql_, timeout_ms_,
        "UPDATE index_batch SET state='FAILED',failure_code=" + quoteSql(failure_code) +
            " WHERE batch_id=" + std::to_string(batch_id));
}

bool IndexStateRepository::recoverInterruptedBatches(std::vector<std::uint64_t>* batch_ids) const {
    if (!queryUnsignedValues(mysql_, timeout_ms_,
                             "SELECT batch_id FROM index_batch WHERE state='PARSING' "
                             "ORDER BY batch_id ASC",
                             batch_ids)) {
        return false;
    }
    for (std::vector<std::uint64_t>::const_iterator it = batch_ids->begin(); it != batch_ids->end();
         ++it) {
        if (!executeUpdate(mysql_, timeout_ms_,
                           "UPDATE indexed_archive SET state='FAILED',document_count=0,"
                           "failure_code='BATCH_INTERRUPTED',failure_line=NULL,indexed_at=NULL "
                           "WHERE batch_id=" +
                               std::to_string(*it)) ||
            !markBatchFailed(*it, "BATCH_INTERRUPTED")) {
            return false;
        }
    }
    return true;
}

RebuildStatus IndexStateRepository::requestRebuild(std::uint64_t archive_id,
                                                   std::uint64_t* batch_id) const {
    std::vector<std::uint64_t> values;
    if (!queryUnsignedValues(mysql_, timeout_ms_,
                             "SELECT batch_id FROM indexed_archive WHERE archive_id=" +
                                 std::to_string(archive_id) + " AND batch_id IS NOT NULL LIMIT 1",
                             &values)) {
        return RebuildStatus::Unavailable;
    }
    if (values.empty()) {
        return RebuildStatus::NotFound;
    }
    *batch_id = values[0];
    if (!executeUpdate(mysql_, timeout_ms_,
                       "UPDATE index_batch SET state='FAILED',failure_code='REBUILD_REQUESTED',"
                       "parsed_path=NULL,parsed_sha256=NULL WHERE batch_id=" +
                           std::to_string(*batch_id)) ||
        !executeUpdate(mysql_, timeout_ms_,
                       "UPDATE indexed_archive SET state='PENDING',document_count=0,"
                       "failure_code=NULL,failure_line=NULL,indexed_at=NULL WHERE batch_id=" +
                           std::to_string(*batch_id))) {
        return RebuildStatus::Unavailable;
    }
    return RebuildStatus::Queued;
}

}  // namespace logtrace
}  // namespace smt
