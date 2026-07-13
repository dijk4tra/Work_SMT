/**
 * @file archive_source_repository.cpp
 * @brief 实现一期归档元数据稳定增量读取。
 */

#include "logtrace/indexing/archive_source_repository.h"

#include <workflow/MySQLResult.h>
#include <workflow/WFFacilities.h>

#include <atomic>
#include <sstream>
#include <vector>

#include "logtrace/common/time_utils.h"

namespace smt {
namespace logtrace {
namespace {

const char kSelectFields[] =
    "archive_id,line_id,station_id,device_id,collector_id,work_order,product_sn,file_type,"
    "original_filename,relative_path,file_size,LOWER(HEX(file_sha256)),produced_at,archived_at";

bool parseRecord(const std::vector<protocol::MySQLCell>& row, ArchiveRecord* record) {
    if (row.size() != 14 || row[0].is_null() || row[1].is_null() || row[2].is_null() ||
        row[3].is_null() || row[4].is_null() || row[7].is_null() || row[8].is_null() ||
        row[9].is_null() || row[10].is_null() || row[11].is_null() || row[12].is_null() ||
        row[13].is_null()) {
        return false;
    }
    record->archive_id = row[0].as_ulonglong();
    record->line_id = row[1].as_string();
    record->station_id = row[2].as_string();
    record->device_id = row[3].as_string();
    record->collector_id = row[4].as_string();
    record->work_order = row[5].is_null() ? "" : row[5].as_string();
    record->product_sn = row[6].is_null() ? "" : row[6].as_string();
    record->file_type = row[7].as_string();
    record->original_filename = row[8].as_string();
    record->relative_path = row[9].as_string();
    record->file_size = row[10].as_ulonglong();
    record->file_sha256 = row[11].as_string();
    return record->archive_id != 0 && record->file_size != 0 && record->file_sha256.size() == 64 &&
           mysqlDateTimeToIso8601(row[12].as_datetime(), &record->produced_at) &&
           mysqlDateTimeToIso8601(row[13].as_datetime(), &record->archived_at);
}

bool executeArchiveQuery(const MySqlClient& mysql, int timeout_ms, const std::string& sql,
                         std::vector<ArchiveRecord>* archives) {
    std::atomic<bool> success(false);
    WFFacilities::WaitGroup wait_group(1);
    WFMySQLTask* task = mysql.createQuery(
        sql, timeout_ms, [&success, &wait_group, archives](WFMySQLTask* completed) {
            if (completed->get_state() != WFT_STATE_SUCCESS ||
                completed->get_resp()->is_error_packet()) {
                wait_group.done();
                return;
            }
            protocol::MySQLResultCursor cursor(completed->get_resp());
            std::vector<protocol::MySQLCell> row;
            while (cursor.fetch_row(row)) {
                ArchiveRecord record;
                if (!parseRecord(row, &record)) {
                    archives->clear();
                    wait_group.done();
                    return;
                }
                archives->push_back(record);
            }
            success.store(true, std::memory_order_release);
            wait_group.done();
        });
    task->start();
    wait_group.wait();
    return success.load(std::memory_order_acquire);
}

}  // namespace

ArchiveSourceRepository::ArchiveSourceRepository(const MySqlClient& mysql, int timeout_ms)
    : mysql_(mysql), timeout_ms_(timeout_ms) {}

bool ArchiveSourceRepository::listAfter(std::uint64_t after_archive_id, std::size_t limit,
                                        std::vector<ArchiveRecord>* archives) const {
    archives->clear();
    std::ostringstream sql;
    sql << "SELECT " << kSelectFields << " FROM archive_file WHERE archive_id>" << after_archive_id
        << " AND file_type IN ('RUNTIME_LOG','TEST_REPORT') ORDER BY archive_id ASC LIMIT "
        << limit;
    return executeArchiveQuery(mysql_, timeout_ms_, sql.str(), archives);
}

bool ArchiveSourceRepository::findByIds(const std::vector<std::uint64_t>& archive_ids,
                                        std::vector<ArchiveRecord>* archives) const {
    archives->clear();
    if (archive_ids.empty()) {
        return true;
    }
    std::ostringstream sql;
    sql << "SELECT " << kSelectFields << " FROM archive_file WHERE archive_id IN (";
    for (std::size_t index = 0; index < archive_ids.size(); ++index) {
        if (index != 0) {
            sql << ',';
        }
        sql << archive_ids[index];
    }
    sql << ") AND file_type IN ('RUNTIME_LOG','TEST_REPORT') ORDER BY archive_id ASC";
    if (!executeArchiveQuery(mysql_, timeout_ms_, sql.str(), archives)) {
        return false;
    }
    if (archives->size() != archive_ids.size()) {
        archives->clear();
        return false;
    }
    for (std::size_t index = 0; index < archives->size(); ++index) {
        if ((*archives)[index].archive_id != archive_ids[index]) {
            archives->clear();
            return false;
        }
    }
    return true;
}

}  // namespace logtrace
}  // namespace smt
