/**
 * @file upload_controller.cpp
 * @brief 实现上传会话创建、分片写入和进度查询接口。
 */

#include "datastream/api/upload_controller.h"

#include <fcntl.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "datastream/archive/archive_model.h"
#include "datastream/archive/archive_storage.h"
#include "datastream/common/api_response.h"
#include "datastream/common/request_id.h"
#include "datastream/common/time_utils.h"
#include "datastream/common/uuid.h"
#include "datastream/upload/upload_model.h"

namespace smt {
namespace datastream {
namespace {

/// @brief 保存创建会话异步链状态。
struct CreateState {
    DeviceAuthResult auth;
    CreateUploadRequest metadata;
    UploadSession session;
    ErrorCode code;
    std::string message;
    bool file_created;
    bool session_created;
};

/// @brief 保存分片写入异步链状态。
struct ChunkState {
    DeviceAuthResult auth;
    UploadSession session;
    std::string upload_id;
    std::string body;
    std::string digest;
    std::size_t chunk_no;
    ErrorCode code;
    std::string message;
    bool already_completed;
};

/// @brief 保存进度查询异步链状态。
struct ProgressState {
    DeviceAuthResult auth;
    UploadSession session;
    std::string upload_id;
    std::string bitmap;
    ErrorCode code;
    std::string message;
};

/// @brief 保存完成上传异步链状态。
struct CompleteState {
    DeviceAuthResult auth;
    UploadSession session;
    ArchiveRecord record;
    std::string upload_id;
    ErrorCode code;
    std::string message;
    bool completed;
};

void pushMarkArchived(const UploadRepository& repository,
                      const std::shared_ptr<CompleteState>& state, SeriesWork* series) {
    WFRedisTask* task = repository.createMarkArchivedTask(
        state->session, state->record.archive_id, state->record.archived_at_milliseconds,
        state->record.relative_path, [state](bool saved) {
            if (!saved) {
                state->code = ErrorCode::RedisUnavailable;
                state->message = "Redis is unavailable";
                return;
            }
            state->completed = true;
        });
    series->push_front(task);
}

bool parseDecimal(const std::string& value, std::uint64_t* result) {
    if (value.empty()) {
        return false;
    }
    std::uint64_t parsed = 0;
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] < '0' || value[index] > '9') {
            return false;
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(value[index] - '0');
        if (parsed > (UINT64_MAX - digit) / 10) {
            return false;
        }
        parsed = parsed * 10 + digit;
    }
    *result = parsed;
    return true;
}

bool allocateFile(const std::string& root, const UploadConfig& config, UploadSession* session,
                  ErrorCode* code, std::string* message) {
    struct statvfs filesystem;
    if (::statvfs(root.c_str(), &filesystem) != 0) {
        *code = ErrorCode::StorageIoError;
        *message = "cannot inspect temporary storage";
        return false;
    }
    const std::uint64_t available =
        static_cast<std::uint64_t>(filesystem.f_bavail) * filesystem.f_frsize;
    const std::uint64_t total =
        static_cast<std::uint64_t>(filesystem.f_blocks) * filesystem.f_frsize;
    if (available < session->file_size + config.min_free_space_bytes ||
        (available - session->file_size) * 100 <
            total * static_cast<std::uint64_t>(config.min_free_space_percent)) {
        *code = ErrorCode::StorageCapacityExceeded;
        *message = "temporary storage is below the safety threshold";
        return false;
    }

    const int descriptor =
        ::open(session->temp_path.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0640);
    if (descriptor < 0) {
        *code = ErrorCode::StorageIoError;
        *message = "cannot create temporary upload file";
        return false;
    }
    const int result = ::posix_fallocate(descriptor, 0, static_cast<off_t>(session->file_size));
    const int close_result = ::close(descriptor);
    if (result != 0 || close_result != 0) {
        ::unlink(session->temp_path.c_str());
        *code = result == ENOSPC ? ErrorCode::StorageCapacityExceeded : ErrorCode::StorageIoError;
        *message = result == ENOSPC ? "temporary storage has insufficient space"
                                    : "cannot allocate temporary upload file";
        return false;
    }
    return true;
}

bool writeChunk(const ChunkState& state) {
    const int descriptor = ::open(state.session.temp_path.c_str(), O_WRONLY | O_CLOEXEC);
    if (descriptor < 0) {
        return false;
    }
    std::size_t written = 0;
    const off_t base =
        static_cast<off_t>(state.chunk_no) * static_cast<off_t>(state.session.chunk_size);
    while (written < state.body.size()) {
        const ssize_t result =
            ::pwrite(descriptor, state.body.data() + written, state.body.size() - written,
                     base + static_cast<off_t>(written));
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            ::close(descriptor);
            return false;
        }
        written += static_cast<std::size_t>(result);
    }
    return ::close(descriptor) == 0;
}

void setLookupError(SessionLookupStatus status, ErrorCode* code, std::string* message) {
    if (status == SessionLookupStatus::NotFound) {
        *code = ErrorCode::UploadNotFound;
        *message = "upload session was not found";
    } else {
        *code = ErrorCode::RedisUnavailable;
        *message = "Redis is unavailable";
    }
}

}  // namespace

UploadController::UploadController(const DeviceAuthenticator& authenticator,
                                   const DeviceRepository& device_repository,
                                   const UploadRepository& upload_repository,
                                   const ArchiveRepository& archive_repository,
                                   const StoragePaths& storage, const AppConfig& config)
    : authenticator_(authenticator),
      device_repository_(device_repository),
      upload_repository_(upload_repository),
      archive_repository_(archive_repository),
      storage_(storage),
      config_(config.upload),
      body_limit_bytes_(config.http.request_body_limit_bytes) {}

void UploadController::registerRoutes(wfrest::HttpServer& server) {
    server.POST("/api/v1/uploads", [this](const wfrest::HttpReq* request,
                                          wfrest::HttpResp* response, SeriesWork* series) {
        if (request->body().size() > body_limit_bytes_) {
            sendApiResponse(response, ErrorCode::InvalidArgument, "request body is too large",
                            generateRequestId(), nullptr);
            return;
        }
        authenticator_.authenticate(
            *request, *series, [this, request, response, series](const DeviceAuthResult& auth) {
                if (auth.code != ErrorCode::Ok) {
                    sendApiResponse(response, auth.code, auth.message, auth.request_id, nullptr);
                    return;
                }
                if (request->content_type() != wfrest::APPLICATION_JSON) {
                    sendApiResponse(response, ErrorCode::InvalidArgument,
                                    "Content-Type must be application/json", auth.request_id,
                                    nullptr);
                    return;
                }
                const std::shared_ptr<CreateState> state(new CreateState());
                state->auth = auth;
                state->code = ErrorCode::Ok;
                state->message = "success";
                state->file_created = false;
                state->session_created = false;
                if (!parseCreateUploadRequest(request->body(), config_, &state->metadata,
                                              &state->message)) {
                    state->code = state->message == "file_size exceeds the configured limit"
                                      ? ErrorCode::FileTooLarge
                                  : state->message == "chunk_size exceeds the configured limit"
                                      ? ErrorCode::ChunkTooLarge
                                      : ErrorCode::InvalidArgument;
                    sendApiResponse(response, state->code, state->message, auth.request_id,
                                    nullptr);
                    return;
                }
                if (state->metadata.station_id != auth.identity.station_id) {
                    sendApiResponse(response, ErrorCode::UploadDeviceMismatch,
                                    "device does not belong to station", auth.request_id, nullptr);
                    return;
                }

                state->session.upload_id = generateUuidV4();
                state->session.state = "UPLOADING";
                state->session.device_id = auth.identity.device_id;
                state->session.station_id = auth.identity.station_id;
                state->session.line_id = auth.identity.line_id;
                state->session.collector_id = state->metadata.collector_id;
                state->session.work_order = state->metadata.work_order;
                state->session.product_sn = state->metadata.product_sn;
                state->session.file_type = state->metadata.file_type;
                state->session.result = state->metadata.result;
                state->session.original_filename = state->metadata.original_filename;
                state->session.extension = state->metadata.extension;
                state->session.temp_path =
                    storage_.tempRoot() + "/" + state->session.upload_id + ".part";
                state->session.relative_path.clear();
                state->session.produced_at = state->metadata.produced_at;
                state->session.file_size = state->metadata.file_size;
                state->session.file_sha256 = state->metadata.file_sha256;
                state->session.chunk_size = state->metadata.chunk_size;
                state->session.chunk_count = state->metadata.chunk_count;
                state->session.expires_at_seconds =
                    currentUnixSeconds() + config_.session_ttl_seconds;
                state->session.failure_code.clear();
                state->session.archive_id = 0;
                state->session.archived_at_milliseconds = 0;

                WFMySQLTask* binding = device_repository_.createBindingCheckTask(
                    state->metadata.collector_id, auth.identity.device_id,
                    [this, state, series](bool available, bool bound) {
                        if (!available) {
                            state->code = ErrorCode::MySqlUnavailable;
                            state->message = "MySQL is unavailable";
                            return;
                        }
                        if (!bound) {
                            state->code = ErrorCode::CollectorDeviceMismatch;
                            state->message = "collector is not authorized for device";
                            return;
                        }
                        WFGoTask* allocate = WFTaskFactory::create_go_task(
                            "datastream-upload-allocation", [this, state]() {
                                state->file_created =
                                    allocateFile(storage_.tempRoot(), config_, &state->session,
                                                 &state->code, &state->message);
                            });
                        allocate->set_callback([this, state, series](WFGoTask*) {
                            if (!state->file_created) {
                                return;
                            }
                            WFRedisTask* create = upload_repository_.createSessionTask(
                                state->session, state->metadata,
                                [state](CreateSessionStatus status) {
                                    if (status == CreateSessionStatus::Created) {
                                        state->session_created = true;
                                    } else if (status == CreateSessionStatus::LimitExceeded) {
                                        state->code = ErrorCode::UploadLimitExceeded;
                                        state->message = "upload session limit was exceeded";
                                    } else {
                                        state->code = ErrorCode::RedisUnavailable;
                                        state->message = "Redis is unavailable";
                                    }
                                });
                            series->push_front(create);
                        });
                        series->push_front(allocate);
                    });
                WFTimerTask* finish =
                    WFTaskFactory::create_timer_task(0, 0, [state, response](WFTimerTask*) {
                        if (!state->session_created) {
                            if (state->file_created) {
                                ::unlink(state->session.temp_path.c_str());
                            }
                            sendApiResponse(response, state->code, state->message,
                                            state->auth.request_id, nullptr);
                            return;
                        }
                        sendApiResponse(
                            response, ErrorCode::Ok, "success", state->auth.request_id,
                            nlohmann::json{
                                {"upload_id", state->session.upload_id},
                                {"state", "UPLOADING"},
                                {"chunk_size", state->session.chunk_size},
                                {"chunk_count", state->session.chunk_count},
                                {"expires_at",
                                 formatUtcMilliseconds(state->session.expires_at_seconds * 1000)}});
                        response->set_status(201);
                    });
                series->push_back(binding);
                series->push_back(finish);
            });
    });

    server.PUT("/api/v1/uploads/{upload_id}/chunks/{chunk_no}", [this](
                                                                    const wfrest::HttpReq* request,
                                                                    wfrest::HttpResp* response,
                                                                    SeriesWork* series) {
        if (request->body().size() > body_limit_bytes_) {
            sendApiResponse(response, ErrorCode::ChunkTooLarge, "chunk body is too large",
                            generateRequestId(), nullptr);
            return;
        }
        authenticator_.authenticate(
            *request, *series, [this, request, response, series](const DeviceAuthResult& auth) {
                if (auth.code != ErrorCode::Ok) {
                    sendApiResponse(response, auth.code, auth.message, auth.request_id, nullptr);
                    return;
                }
                std::uint64_t chunk_no = 0;
                std::uint64_t content_length = 0;
                if (!isUuid(request->param("upload_id")) ||
                    !parseDecimal(request->param("chunk_no"), &chunk_no) ||
                    !request->has_header("Content-Length") ||
                    !parseDecimal(request->header("Content-Length"), &content_length) ||
                    content_length != request->body().size() ||
                    request->content_type() != wfrest::APPLICATION_OCTET_STREAM) {
                    sendApiResponse(response, ErrorCode::InvalidArgument,
                                    "chunk request is invalid", auth.request_id, nullptr);
                    return;
                }
                const std::shared_ptr<ChunkState> state(new ChunkState());
                state->auth = auth;
                state->upload_id = request->param("upload_id");
                state->body = request->body();
                state->digest = request->header("X-Content-SHA256");
                state->chunk_no = static_cast<std::size_t>(chunk_no);
                state->code = ErrorCode::Ok;
                state->message = "success";
                state->already_completed = false;

                WFRedisTask* lookup = upload_repository_.createSessionLookupTask(
                    state->upload_id, [this, state, series](SessionLookupStatus status,
                                                            const UploadSession& session) {
                        if (status != SessionLookupStatus::Found) {
                            setLookupError(status, &state->code, &state->message);
                            return;
                        }
                        state->session = session;
                        if (session.device_id != state->auth.identity.device_id) {
                            state->code = ErrorCode::UploadDeviceMismatch;
                            state->message = "upload session belongs to another device";
                            return;
                        }
                        if (session.state != "UPLOADING") {
                            state->code = ErrorCode::UploadStateConflict;
                            state->message = "upload session is not writable";
                            return;
                        }
                        if (state->chunk_no >= session.chunk_count) {
                            state->code = ErrorCode::InvalidArgument;
                            state->message = "chunk_no is out of range";
                            return;
                        }
                        const std::uint64_t offset = state->chunk_no * session.chunk_size;
                        const std::uint64_t expected = state->chunk_no + 1 == session.chunk_count
                                                           ? session.file_size - offset
                                                           : session.chunk_size;
                        if (state->body.size() != expected) {
                            state->code = ErrorCode::InvalidArgument;
                            state->message = "chunk length does not match session";
                            return;
                        }
                        WFRedisTask* begin = upload_repository_.createBeginChunkTask(
                            state->upload_id, state->chunk_no, state->digest,
                            [this, state, series](BeginChunkStatus begin_status) {
                                if (begin_status == BeginChunkStatus::AlreadyComplete) {
                                    state->already_completed = true;
                                    return;
                                }
                                if (begin_status != BeginChunkStatus::Writable) {
                                    if (begin_status == BeginChunkStatus::ContentConflict) {
                                        state->code = ErrorCode::ChunkContentConflict;
                                        state->message =
                                            "chunk content conflicts with completed data";
                                    } else if (begin_status == BeginChunkStatus::StateConflict) {
                                        state->code = ErrorCode::UploadStateConflict;
                                        state->message = "upload session is not writable";
                                    } else if (begin_status == BeginChunkStatus::NotFound) {
                                        state->code = ErrorCode::UploadNotFound;
                                        state->message = "upload session was not found";
                                    } else {
                                        state->code = ErrorCode::RedisUnavailable;
                                        state->message = "Redis is unavailable";
                                    }
                                    return;
                                }
                                WFGoTask* write = WFTaskFactory::create_go_task(
                                    "datastream-upload-" + state->upload_id, [state]() {
                                        if (!writeChunk(*state)) {
                                            state->code = ErrorCode::StorageIoError;
                                            state->message = "cannot write chunk";
                                        }
                                    });
                                write->set_callback([this, state, series](WFGoTask*) {
                                    if (state->code != ErrorCode::Ok) {
                                        return;
                                    }
                                    WFRedisTask* complete =
                                        upload_repository_.createFinishChunkTask(
                                            state->session, state->chunk_no, state->digest,
                                            [state](bool saved) {
                                                if (!saved) {
                                                    state->code = ErrorCode::RedisUnavailable;
                                                    state->message = "Redis is unavailable";
                                                }
                                            });
                                    series->push_front(complete);
                                });
                                series->push_front(write);
                            });
                        series->push_front(begin);
                    });
                WFTimerTask* finish =
                    WFTaskFactory::create_timer_task(0, 0, [state, response](WFTimerTask*) {
                        if (state->code != ErrorCode::Ok) {
                            sendApiResponse(response, state->code, state->message,
                                            state->auth.request_id, nullptr);
                            return;
                        }
                        sendApiResponse(
                            response, ErrorCode::Ok, "success", state->auth.request_id,
                            nlohmann::json{{"chunk_no", state->chunk_no},
                                           {"already_completed", state->already_completed}});
                    });
                series->push_back(lookup);
                series->push_back(finish);
            });
    });

    server.GET("/api/v1/uploads/{upload_id}", [this](const wfrest::HttpReq* request,
                                                     wfrest::HttpResp* response,
                                                     SeriesWork* series) {
        authenticator_.authenticate(
            *request, *series, [this, request, response, series](const DeviceAuthResult& auth) {
                if (auth.code != ErrorCode::Ok) {
                    sendApiResponse(response, auth.code, auth.message, auth.request_id, nullptr);
                    return;
                }
                if (!isUuid(request->param("upload_id"))) {
                    sendApiResponse(response, ErrorCode::InvalidArgument, "upload_id is invalid",
                                    auth.request_id, nullptr);
                    return;
                }
                const std::shared_ptr<ProgressState> state(new ProgressState());
                state->auth = auth;
                state->upload_id = request->param("upload_id");
                state->code = ErrorCode::Ok;
                state->message = "success";
                WFRedisTask* lookup = upload_repository_.createSessionLookupTask(
                    state->upload_id, [this, state, series](SessionLookupStatus status,
                                                            const UploadSession& session) {
                        if (status != SessionLookupStatus::Found) {
                            setLookupError(status, &state->code, &state->message);
                            return;
                        }
                        state->session = session;
                        if (session.device_id != state->auth.identity.device_id) {
                            state->code = ErrorCode::UploadDeviceMismatch;
                            state->message = "upload session belongs to another device";
                            return;
                        }
                        WFRedisTask* bitmap = upload_repository_.createBitmapTask(
                            state->upload_id, [state](bool available, const std::string& value) {
                                if (!available) {
                                    state->code = ErrorCode::RedisUnavailable;
                                    state->message = "Redis is unavailable";
                                    return;
                                }
                                state->bitmap = value;
                            });
                        series->push_front(bitmap);
                    });
                WFTimerTask* finish =
                    WFTaskFactory::create_timer_task(0, 0, [state, response](WFTimerTask*) {
                        if (state->code != ErrorCode::Ok) {
                            sendApiResponse(response, state->code, state->message,
                                            state->auth.request_id, nullptr);
                            return;
                        }
                        nlohmann::json completed = nlohmann::json::array();
                        nlohmann::json missing = nlohmann::json::array();
                        for (std::size_t chunk = 0; chunk < state->session.chunk_count; ++chunk) {
                            (bitmapContains(state->bitmap, chunk) ? completed : missing)
                                .push_back(chunk);
                        }
                        sendApiResponse(
                            response, ErrorCode::Ok, "success", state->auth.request_id,
                            nlohmann::json{
                                {"upload_id", state->upload_id},
                                {"state", state->session.state},
                                {"chunk_count", state->session.chunk_count},
                                {"completed_chunks", completed},
                                {"missing_chunks", missing},
                                {"expires_at",
                                 formatUtcMilliseconds(state->session.expires_at_seconds * 1000)},
                                {"failure_code",
                                 state->session.failure_code.empty()
                                     ? nlohmann::json(nullptr)
                                     : nlohmann::json(state->session.failure_code)}});
                    });
                series->push_back(lookup);
                series->push_back(finish);
            });
    });

    server.POST("/api/v1/uploads/{upload_id}/complete", [this](const wfrest::HttpReq* request,
                                                               wfrest::HttpResp* response,
                                                               SeriesWork* series) {
        authenticator_.authenticate(
            *request, *series, [this, request, response, series](const DeviceAuthResult& auth) {
                if (auth.code != ErrorCode::Ok) {
                    sendApiResponse(response, auth.code, auth.message, auth.request_id, nullptr);
                    return;
                }
                if (!request->body().empty() || !isUuid(request->param("upload_id"))) {
                    sendApiResponse(response, ErrorCode::InvalidArgument,
                                    "complete request is invalid", auth.request_id, nullptr);
                    return;
                }
                const std::shared_ptr<CompleteState> state(new CompleteState());
                state->auth = auth;
                state->upload_id = request->param("upload_id");
                state->code = ErrorCode::Ok;
                state->message = "success";
                state->completed = false;

                WFRedisTask* lookup = upload_repository_.createSessionLookupTask(
                    state->upload_id, [this, state, series](SessionLookupStatus status,
                                                            const UploadSession& session) {
                        if (status != SessionLookupStatus::Found) {
                            setLookupError(status, &state->code, &state->message);
                            return;
                        }
                        state->session = session;
                        if (session.device_id != state->auth.identity.device_id) {
                            state->code = ErrorCode::UploadDeviceMismatch;
                            state->message = "upload session belongs to another device";
                            return;
                        }
                        if (session.state == "FAILED") {
                            state->code = ErrorCode::UploadStateConflict;
                            state->message = "upload session has failed";
                            return;
                        }
                        if (session.state == "ARCHIVED") {
                            if (session.archive_id == 0 || session.archived_at_milliseconds == 0) {
                                state->code = ErrorCode::RedisUnavailable;
                                state->message = "Redis upload state is invalid";
                                return;
                            }
                            state->record.archive_id = session.archive_id;
                            state->record.upload_id = session.upload_id;
                            state->record.archived_at_milliseconds =
                                session.archived_at_milliseconds;
                            state->record.archived_at =
                                formatUtcMilliseconds(session.archived_at_milliseconds);
                            state->completed = true;
                            return;
                        }
                        const ServerTime now = currentServerTime();
                        std::int64_t now_milliseconds = 0;
                        parseIso8601Milliseconds(now.iso8601, &now_milliseconds);
                        const std::int64_t archived_at = session.state == "VERIFYING"
                                                             ? session.archived_at_milliseconds
                                                             : now_milliseconds;
                        const std::string relative =
                            session.state == "VERIFYING"
                                ? session.relative_path
                                : buildArchiveRelativePath(session, archived_at);
                        if (archived_at == 0 || relative.empty()) {
                            state->code = ErrorCode::RedisUnavailable;
                            state->message = "Redis upload state is invalid";
                            return;
                        }
                        WFRedisTask* begin = upload_repository_.createBeginCompleteTask(
                            session.upload_id, session.chunk_count, archived_at, relative,
                            [this, state, series, archived_at,
                             relative](BeginCompleteStatus begin_status) {
                                if (begin_status == BeginCompleteStatus::ChunksIncomplete) {
                                    state->code = ErrorCode::ChunksIncomplete;
                                    state->message = "upload chunks are incomplete";
                                    return;
                                }
                                if (begin_status == BeginCompleteStatus::NotFound) {
                                    state->code = ErrorCode::UploadNotFound;
                                    state->message = "upload session was not found";
                                    return;
                                }
                                if (begin_status == BeginCompleteStatus::Unavailable) {
                                    state->code = ErrorCode::RedisUnavailable;
                                    state->message = "Redis is unavailable";
                                    return;
                                }
                                if (begin_status == BeginCompleteStatus::Failed) {
                                    state->code = ErrorCode::UploadStateConflict;
                                    state->message = "upload session has failed";
                                    return;
                                }
                                state->session.state = "VERIFYING";
                                state->session.archived_at_milliseconds = archived_at;
                                state->session.relative_path = relative;
                                WFMySQLTask* existing = archive_repository_.createFindByUploadTask(
                                    state->upload_id,
                                    [this, state, series](ArchiveLookupStatus archive_status,
                                                          const ArchiveRecord& record) {
                                        if (archive_status == ArchiveLookupStatus::Found) {
                                            state->record = record;
                                            pushMarkArchived(upload_repository_, state, series);
                                            return;
                                        }
                                        if (archive_status == ArchiveLookupStatus::Unavailable) {
                                            state->code = ErrorCode::MySqlUnavailable;
                                            state->message = "MySQL is unavailable";
                                            return;
                                        }
                                        WFGoTask* archive = WFTaskFactory::create_go_task(
                                            "datastream-complete-" + state->upload_id,
                                            [this, state]() {
                                                const ArchiveStorageResult result =
                                                    verifyAndArchiveFile(
                                                        storage_, state->session,
                                                        state->session.relative_path,
                                                        config_.hash_mmap_window_bytes);
                                                if (result.status ==
                                                    ArchiveStorageStatus::IntegrityMismatch) {
                                                    state->code = ErrorCode::FileIntegrityMismatch;
                                                    state->message =
                                                        "file size or SHA-256 does not match";
                                                } else if (result.status ==
                                                           ArchiveStorageStatus::IoError) {
                                                    state->code = ErrorCode::StorageIoError;
                                                    state->message =
                                                        "cannot verify or archive file";
                                                } else {
                                                    state->record = makeArchiveRecord(
                                                        state->session, result.relative_path,
                                                        state->session.archived_at_milliseconds);
                                                }
                                            });
                                        archive->set_callback([this, state, series](WFGoTask*) {
                                            if (state->code == ErrorCode::FileIntegrityMismatch) {
                                                WFRedisTask* failed =
                                                    upload_repository_.createMarkFailedTask(
                                                        state->session, "FILE_INTEGRITY_MISMATCH",
                                                        [state](bool saved) {
                                                            if (!saved) {
                                                                state->code =
                                                                    ErrorCode::RedisUnavailable;
                                                                state->message =
                                                                    "Redis is unavailable";
                                                            }
                                                        });
                                                series->push_front(failed);
                                                return;
                                            }
                                            if (state->code != ErrorCode::Ok) {
                                                return;
                                            }
                                            WFMySQLTask* insert =
                                                archive_repository_.createInsertTask(
                                                    state->record,
                                                    [this, state, series](
                                                        ArchiveInsertStatus insert_status,
                                                        std::uint64_t archive_id) {
                                                        if (insert_status ==
                                                            ArchiveInsertStatus::Inserted) {
                                                            state->record.archive_id = archive_id;
                                                            pushMarkArchived(upload_repository_,
                                                                             state, series);
                                                            return;
                                                        }
                                                        if (insert_status ==
                                                            ArchiveInsertStatus::Unavailable) {
                                                            state->code =
                                                                ErrorCode::MySqlUnavailable;
                                                            state->message = "MySQL is unavailable";
                                                            return;
                                                        }
                                                        WFMySQLTask* conflict =
                                                            archive_repository_
                                                                .createFindByUploadTask(
                                                                    state->upload_id,
                                                                    [this, state, series](
                                                                        ArchiveLookupStatus status,
                                                                        const ArchiveRecord&
                                                                            existing_record) {
                                                                        if (status ==
                                                                            ArchiveLookupStatus::
                                                                                Found) {
                                                                            state->record =
                                                                                existing_record;
                                                                            pushMarkArchived(
                                                                                upload_repository_,
                                                                                state, series);
                                                                        } else {
                                                                            state
                                                                                ->code = ErrorCode::
                                                                                MySqlUnavailable;
                                                                            state->message =
                                                                                "cannot resolve "
                                                                                "archive conflict";
                                                                        }
                                                                    });
                                                        series->push_front(conflict);
                                                    });
                                            series->push_front(insert);
                                        });
                                        series->push_front(archive);
                                    });
                                series->push_front(existing);
                            });
                        series->push_front(begin);
                    });
                WFTimerTask* finish =
                    WFTaskFactory::create_timer_task(0, 0, [state, response](WFTimerTask*) {
                        if (!state->completed) {
                            sendApiResponse(response, state->code, state->message,
                                            state->auth.request_id, nullptr);
                            return;
                        }
                        sendApiResponse(response, ErrorCode::Ok, "success", state->auth.request_id,
                                        nlohmann::json{{"archive_id", state->record.archive_id},
                                                       {"upload_id", state->upload_id},
                                                       {"state", "ARCHIVED"},
                                                       {"archived_at", state->record.archived_at}});
                    });
                series->push_back(lookup);
                series->push_back(finish);
            });
    });
}

}  // namespace datastream
}  // namespace smt
