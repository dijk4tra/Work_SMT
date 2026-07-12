/**
 * @file archive_repository.cpp
 * @brief 实现 MySQL 归档元数据写入和稳定分页查询。
 */

#include "datastream/archive/archive_repository.h"

#include <workflow/MySQLResult.h>
#include <workflow/MySQLUtil.h>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

#include "datastream/common/time_utils.h"

namespace smt {
namespace datastream {
namespace {

const char kSelectFields[] =
    "archive_id,upload_id,line_id,station_id,device_id,collector_id,work_order,product_sn,"
    "file_type,result,original_filename,relative_path,file_size,LOWER(HEX(file_sha256)),"
    "produced_at,archived_at";

std::string quoteSql(const std::string& value) {
    return "'" + protocol::MySQLUtil::escape_string_quote(value, '\'') + "'";
}

std::string nullableSql(const std::string& value) {
    return value.empty() ? "NULL" : quoteSql(value);
}

bool parseRecord(const std::vector<protocol::MySQLCell>& row, ArchiveRecord* record) {
    if (row.size() != 16 || row[0].is_null() || row[12].is_null() || row[14].is_null() ||
        row[15].is_null()) {
        return false;
    }
    record->archive_id = row[0].as_ulonglong();
    record->upload_id = row[1].as_string();
    record->line_id = row[2].as_string();
    record->station_id = row[3].as_string();
    record->device_id = row[4].as_string();
    record->collector_id = row[5].as_string();
    record->work_order = row[6].is_null() ? "" : row[6].as_string();
    record->product_sn = row[7].is_null() ? "" : row[7].as_string();
    record->file_type = row[8].as_string();
    record->result = row[9].is_null() ? "" : row[9].as_string();
    record->original_filename = row[10].as_string();
    record->relative_path = row[11].as_string();
    record->file_size = row[12].as_ulonglong();
    record->file_sha256 = row[13].as_string();
    if (!mysqlDateTimeToIso8601(row[14].as_datetime(), &record->produced_at) ||
        !mysqlDateTimeToIso8601(row[15].as_datetime(), &record->archived_at) ||
        !parseIso8601Milliseconds(record->archived_at, &record->archived_at_milliseconds)) {
        return false;
    }
    return record->archive_id != 0 && record->file_sha256.size() == 64;
}

WFMySQLTask* createLookup(
    const MySqlClient& mysql, int timeout_ms, const std::string& where,
    const std::function<void(ArchiveLookupStatus, const ArchiveRecord&)>& callback) {
    const std::string sql =
        std::string("SELECT ") + kSelectFields + " FROM archive_file WHERE " + where + " LIMIT 1";
    return mysql.createQuery(sql, timeout_ms, [callback](WFMySQLTask* task) {
        if (task->get_state() != WFT_STATE_SUCCESS || task->get_resp()->is_error_packet()) {
            callback(ArchiveLookupStatus::Unavailable, ArchiveRecord());
            return;
        }
        protocol::MySQLResultCursor cursor(task->get_resp());
        std::vector<protocol::MySQLCell> row;
        if (!cursor.fetch_row(row)) {
            callback(ArchiveLookupStatus::NotFound, ArchiveRecord());
            return;
        }
        ArchiveRecord record;
        if (!parseRecord(row, &record)) {
            callback(ArchiveLookupStatus::Unavailable, ArchiveRecord());
            return;
        }
        callback(ArchiveLookupStatus::Found, record);
    });
}

}  // namespace

ArchiveRepository::ArchiveRepository(const MySqlClient& mysql, int timeout_ms)
    : mysql_(mysql), timeout_ms_(timeout_ms) {}

WFMySQLTask* ArchiveRepository::createFindByUploadTask(
    const std::string& upload_id,
    const std::function<void(ArchiveLookupStatus, const ArchiveRecord&)>& callback) const {
    return createLookup(mysql_, timeout_ms_, "upload_id=" + quoteSql(upload_id), callback);
}

WFMySQLTask* ArchiveRepository::createFindByIdTask(
    std::uint64_t archive_id,
    const std::function<void(ArchiveLookupStatus, const ArchiveRecord&)>& callback) const {
    return createLookup(mysql_, timeout_ms_, "archive_id=" + std::to_string(archive_id), callback);
}

WFMySQLTask* ArchiveRepository::createInsertTask(
    const ArchiveRecord& record,
    const std::function<void(ArchiveInsertStatus, std::uint64_t)>& callback) const {
    std::int64_t produced_at = 0;
    parseIso8601Milliseconds(record.produced_at, &produced_at);
    const std::string sql =
        "INSERT INTO archive_file(upload_id,line_id,station_id,device_id,collector_id,work_order,"
        "product_sn,file_type,result,original_filename,relative_path,file_size,file_sha256,"
        "produced_at,archived_at) VALUES(" +
        quoteSql(record.upload_id) + "," + quoteSql(record.line_id) + "," +
        quoteSql(record.station_id) + "," + quoteSql(record.device_id) + "," +
        quoteSql(record.collector_id) + "," + nullableSql(record.work_order) + "," +
        nullableSql(record.product_sn) + "," + quoteSql(record.file_type) + "," +
        nullableSql(record.result) + "," + quoteSql(record.original_filename) + "," +
        quoteSql(record.relative_path) + "," + std::to_string(record.file_size) + ",UNHEX(" +
        quoteSql(record.file_sha256) + ")," + quoteSql(formatMysqlMilliseconds(produced_at)) + "," +
        quoteSql(formatMysqlMilliseconds(record.archived_at_milliseconds)) + ")";
    return mysql_.createQuery(sql, timeout_ms_, [callback](WFMySQLTask* task) {
        if (task->get_state() != WFT_STATE_SUCCESS) {
            callback(ArchiveInsertStatus::Unavailable, 0);
            return;
        }
        if (task->get_resp()->is_error_packet()) {
            callback(task->get_resp()->get_error_code() == 1062 ? ArchiveInsertStatus::Conflict
                                                                : ArchiveInsertStatus::Unavailable,
                     0);
            return;
        }
        protocol::MySQLResultCursor cursor(task->get_resp());
        callback(ArchiveInsertStatus::Inserted, cursor.get_insert_id());
    });
}

WFMySQLTask* ArchiveRepository::createListTask(
    const ArchiveQuery& query,
    const std::function<void(bool, const ArchivePage&)>& callback) const {
    std::vector<std::string> conditions;
    if (!query.device_id.empty()) conditions.push_back("device_id=" + quoteSql(query.device_id));
    if (!query.station_id.empty()) conditions.push_back("station_id=" + quoteSql(query.station_id));
    if (!query.work_order.empty()) conditions.push_back("work_order=" + quoteSql(query.work_order));
    if (!query.product_sn.empty()) conditions.push_back("product_sn=" + quoteSql(query.product_sn));
    if (!query.file_type.empty()) conditions.push_back("file_type=" + quoteSql(query.file_type));
    if (!query.result.empty()) conditions.push_back("result=" + quoteSql(query.result));
    if (query.has_archived_from) {
        conditions.push_back("archived_at>=" +
                             quoteSql(formatMysqlMilliseconds(query.archived_from_milliseconds)));
    }
    if (query.has_archived_to) {
        conditions.push_back("archived_at<=" +
                             quoteSql(formatMysqlMilliseconds(query.archived_to_milliseconds)));
    }
    if (query.has_cursor) {
        const std::string time =
            quoteSql(formatMysqlMilliseconds(query.cursor.archived_at_milliseconds));
        conditions.push_back("(archived_at<" + time + " OR (archived_at=" + time +
                             " AND archive_id<" + std::to_string(query.cursor.archive_id) + "))");
    }
    std::ostringstream sql;
    sql << "SELECT " << kSelectFields << " FROM archive_file";
    if (!conditions.empty()) {
        sql << " WHERE ";
        for (std::size_t index = 0; index < conditions.size(); ++index) {
            if (index != 0) sql << " AND ";
            sql << conditions[index];
        }
    }
    sql << " ORDER BY archived_at DESC,archive_id DESC LIMIT " << query.page_size + 1;
    return mysql_.createQuery(sql.str(), timeout_ms_, [query, callback](WFMySQLTask* task) {
        if (task->get_state() != WFT_STATE_SUCCESS || task->get_resp()->is_error_packet()) {
            callback(false, ArchivePage());
            return;
        }
        protocol::MySQLResultCursor cursor(task->get_resp());
        ArchivePage page;
        std::vector<protocol::MySQLCell> row;
        while (cursor.fetch_row(row)) {
            ArchiveRecord record;
            if (!parseRecord(row, &record)) {
                callback(false, ArchivePage());
                return;
            }
            page.items.push_back(record);
        }
        page.has_more = page.items.size() > static_cast<std::size_t>(query.page_size);
        if (page.has_more) page.items.resize(static_cast<std::size_t>(query.page_size));
        callback(true, page);
    });
}

}  // namespace datastream
}  // namespace smt
