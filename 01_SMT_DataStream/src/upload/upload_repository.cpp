/**
 * @file upload_repository.cpp
 * @brief 实现 Redis 上传会话、配额、分片摘要和 Bitmap 操作。
 */

#include "datastream/upload/upload_repository.h"

#include <cstdint>

#include "datastream/common/time_utils.h"

namespace smt {
namespace datastream {
namespace {

const char kCreateSessionScript[] =
    "local function clean(k,n) local v=redis.call('ZRANGEBYSCORE',k,'-inf',n);"
    "if #v>0 then redis.call('ZREM',k,unpack(v)) end end;"
    "clean(KEYS[2],ARGV[1]);clean(KEYS[3],ARGV[1]);clean(KEYS[4],ARGV[1]);"
    "if redis.call('EXISTS',KEYS[1])==1 then return 2 end;"
    "if redis.call('ZCARD',KEYS[2])>=tonumber(ARGV[3]) then return 3 end;"
    "if redis.call('ZCARD',KEYS[3])>=tonumber(ARGV[4]) then return 4 end;"
    "if redis.call('ZCARD',KEYS[4])>=tonumber(ARGV[5]) then return 5 end;"
    "local members=redis.call('ZRANGE',KEYS[2],0,-1);local bytes=0;"
    "for _,m in ipairs(members) do local p=string.match(m,':(%d+)$');bytes=bytes+tonumber(p) end;"
    "if bytes+tonumber(ARGV[6])>tonumber(ARGV[7]) then return 6 end;"
    "redis.call('HSET',KEYS[1],unpack(ARGV,9));redis.call('EXPIRE',KEYS[1],ARGV[8]);"
    "local member=ARGV[10]..':'..ARGV[6];redis.call('ZADD',KEYS[2],ARGV[2],member);"
    "redis.call('ZADD',KEYS[3],ARGV[2],member);redis.call('ZADD',KEYS[4],ARGV[2],member);return 1";

const char kBeginChunkScript[] =
    "if redis.call('EXISTS',KEYS[1])==0 then return 4 end;"
    "if redis.call('HGET',KEYS[1],'state')~='UPLOADING' then return 3 end;"
    "local old=redis.call('HGET',KEYS[3],ARGV[1]);"
    "if old and old~=ARGV[2] then return 2 end;"
    "if old and redis.call('GETBIT',KEYS[2],ARGV[1])==1 then return 1 end;"
    "if not old then redis.call('HSET',KEYS[3],ARGV[1],ARGV[2]) end;"
    "redis.call('EXPIRE',KEYS[3],ARGV[3]);return 0";

const char kFinishChunkScript[] =
    "if redis.call('EXISTS',KEYS[1])==0 or redis.call('HGET',KEYS[1],'state')~='UPLOADING' "
    "or redis.call('HGET',KEYS[3],ARGV[1])~=ARGV[2] then return 0 end;"
    "redis.call('SETBIT',KEYS[2],ARGV[1],1);redis.call('HSET',KEYS[1],'expires_at',ARGV[4]);"
    "redis.call('EXPIRE',KEYS[1],ARGV[3]);redis.call('EXPIRE',KEYS[2],ARGV[3]);"
    "redis.call('EXPIRE',KEYS[3],ARGV[3]);redis.call('ZADD',KEYS[4],ARGV[4],ARGV[5]);"
    "redis.call('ZADD',KEYS[5],ARGV[4],ARGV[5]);redis.call('ZADD',KEYS[6],ARGV[4],ARGV[5]);return "
    "1";

bool redisInteger(WFRedisTask* task, std::int64_t* result) {
    if (task->get_state() != WFT_STATE_SUCCESS) {
        return false;
    }
    protocol::RedisValue value;
    task->get_resp()->get_result(value);
    if (!value.is_int()) {
        return false;
    }
    *result = value.int_value();
    return true;
}

bool redisStringArray(WFRedisTask* task, std::vector<std::string>* values) {
    if (task->get_state() != WFT_STATE_SUCCESS) {
        return false;
    }
    protocol::RedisValue result;
    task->get_resp()->get_result(result);
    if (!result.is_array()) {
        return false;
    }
    for (std::size_t index = 0; index < result.arr_size(); ++index) {
        protocol::RedisValue& item = result.arr_at(index);
        if (!item.is_string()) {
            return false;
        }
        values->push_back(item.string_value());
    }
    return true;
}

}  // namespace

UploadRepository::UploadRepository(const RedisClient& redis, const RedisConfig& redis_config,
                                   const UploadConfig& upload_config, int timeout_ms)
    : redis_(redis),
      key_prefix_(redis_config.key_prefix),
      config_(upload_config),
      timeout_ms_(timeout_ms) {}

WFRedisTask* UploadRepository::createSessionTask(
    const UploadSession& session, const CreateUploadRequest& metadata,
    const std::function<void(CreateSessionStatus)>& callback) const {
    const std::string base = key_prefix_ + "upload:";
    std::vector<std::string> params{
        kCreateSessionScript,
        "4",
        base + session.upload_id,
        base + "quota:global",
        base + "quota:device:" + session.device_id,
        base + "quota:collector:" + session.collector_id,
        std::to_string(currentUnixSeconds()),
        std::to_string(session.expires_at_seconds),
        std::to_string(config_.max_active_sessions),
        std::to_string(config_.max_device_sessions),
        std::to_string(config_.max_collector_sessions),
        std::to_string(session.file_size),
        std::to_string(config_.max_reserved_bytes),
        std::to_string(config_.session_ttl_seconds),
        "upload_id",
        session.upload_id,
        "state",
        "UPLOADING",
        "device_id",
        session.device_id,
        "station_id",
        session.station_id,
        "line_id",
        session.line_id,
        "collector_id",
        session.collector_id,
        "work_order",
        metadata.work_order,
        "product_sn",
        metadata.product_sn,
        "file_type",
        metadata.file_type,
        "result",
        metadata.result,
        "original_filename",
        metadata.original_filename,
        "extension",
        metadata.extension,
        "file_size",
        std::to_string(session.file_size),
        "file_sha256",
        session.file_sha256,
        "chunk_size",
        std::to_string(session.chunk_size),
        "chunk_count",
        std::to_string(session.chunk_count),
        "temp_path",
        session.temp_path,
        "produced_at",
        metadata.produced_at,
        "created_at",
        std::to_string(session.expires_at_seconds - config_.session_ttl_seconds),
        "expires_at",
        std::to_string(session.expires_at_seconds),
        "failure_code",
        ""};
    return redis_.createCommand("EVAL", params, timeout_ms_, [callback](WFRedisTask* task) {
        std::int64_t result = 0;
        if (!redisInteger(task, &result)) {
            callback(CreateSessionStatus::Unavailable);
        } else if (result == 1) {
            callback(CreateSessionStatus::Created);
        } else {
            callback(CreateSessionStatus::LimitExceeded);
        }
    });
}

WFRedisTask* UploadRepository::createSessionLookupTask(
    const std::string& upload_id,
    const std::function<void(SessionLookupStatus, const UploadSession&)>& callback) const {
    return redis_.createCommand("HGETALL", {key_prefix_ + "upload:" + upload_id}, timeout_ms_,
                                [callback](WFRedisTask* task) {
                                    std::vector<std::string> values;
                                    if (!redisStringArray(task, &values)) {
                                        callback(SessionLookupStatus::Unavailable, UploadSession());
                                        return;
                                    }
                                    if (values.empty()) {
                                        callback(SessionLookupStatus::NotFound, UploadSession());
                                        return;
                                    }
                                    UploadSession session;
                                    if (!parseUploadSession(values, &session)) {
                                        callback(SessionLookupStatus::Unavailable, UploadSession());
                                        return;
                                    }
                                    callback(SessionLookupStatus::Found, session);
                                });
}

WFRedisTask* UploadRepository::createBeginChunkTask(
    const std::string& upload_id, std::size_t chunk_no, const std::string& digest,
    const std::function<void(BeginChunkStatus)>& callback) const {
    const std::string base = key_prefix_ + "upload:" + upload_id;
    std::vector<std::string> params{kBeginChunkScript,
                                    "3",
                                    base,
                                    base + ":chunks",
                                    base + ":digests",
                                    std::to_string(chunk_no),
                                    digest,
                                    std::to_string(config_.session_ttl_seconds)};
    return redis_.createCommand("EVAL", params, timeout_ms_, [callback](WFRedisTask* task) {
        std::int64_t result = 0;
        if (!redisInteger(task, &result)) {
            callback(BeginChunkStatus::Unavailable);
        } else if (result == 0) {
            callback(BeginChunkStatus::Writable);
        } else if (result == 1) {
            callback(BeginChunkStatus::AlreadyComplete);
        } else if (result == 2) {
            callback(BeginChunkStatus::ContentConflict);
        } else if (result == 3) {
            callback(BeginChunkStatus::StateConflict);
        } else {
            callback(BeginChunkStatus::NotFound);
        }
    });
}

WFRedisTask* UploadRepository::createFinishChunkTask(
    const UploadSession& session, std::size_t chunk_no, const std::string& digest,
    const std::function<void(bool)>& callback) const {
    const std::string root = key_prefix_ + "upload:";
    const std::string base = root + session.upload_id;
    const std::int64_t expires_at = currentUnixSeconds() + config_.session_ttl_seconds;
    const std::string member = session.upload_id + ":" + std::to_string(session.file_size);
    std::vector<std::string> params{kFinishChunkScript,
                                    "6",
                                    base,
                                    base + ":chunks",
                                    base + ":digests",
                                    root + "quota:global",
                                    root + "quota:device:" + session.device_id,
                                    root + "quota:collector:" + session.collector_id,
                                    std::to_string(chunk_no),
                                    digest,
                                    std::to_string(config_.session_ttl_seconds),
                                    std::to_string(expires_at),
                                    member};
    return redis_.createCommand("EVAL", params, timeout_ms_, [callback](WFRedisTask* task) {
        std::int64_t result = 0;
        callback(redisInteger(task, &result) && result == 1);
    });
}

WFRedisTask* UploadRepository::createBitmapTask(
    const std::string& upload_id,
    const std::function<void(bool, const std::string&)>& callback) const {
    return redis_.createCommand("GET", {key_prefix_ + "upload:" + upload_id + ":chunks"},
                                timeout_ms_, [callback](WFRedisTask* task) {
                                    if (task->get_state() != WFT_STATE_SUCCESS) {
                                        callback(false, "");
                                        return;
                                    }
                                    protocol::RedisValue value;
                                    task->get_resp()->get_result(value);
                                    callback(!value.is_error(),
                                             value.is_nil() ? std::string() : value.string_value());
                                });
}

bool bitmapContains(const std::string& bitmap, std::size_t chunk_no) {
    const std::size_t byte_index = chunk_no / 8;
    if (byte_index >= bitmap.size()) {
        return false;
    }
    const unsigned char value = static_cast<unsigned char>(bitmap[byte_index]);
    return (value & static_cast<unsigned char>(1U << (7U - chunk_no % 8U))) != 0;
}

}  // namespace datastream
}  // namespace smt
