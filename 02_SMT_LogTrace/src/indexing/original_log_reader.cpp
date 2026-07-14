/**
 * @file original_log_reader.cpp
 * @brief 实现归档 SHA-256 复核和循环 pread 原文回读。
 */

#include "logtrace/indexing/original_log_reader.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>

#include "logtrace/common/sha256.h"

namespace smt {
namespace logtrace {
namespace {

int openVerified(const StoragePaths& storage, const SegmentFileRecord& file) {
    const std::string path = storage.resolveArchiveFile(file.relative_path);
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        throw std::runtime_error("cannot open indexed archive: " +
                                 std::string(std::strerror(errno)));
    }
    struct stat info;
    if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0 ||
        static_cast<std::uint64_t>(info.st_size) != file.file_size) {
        ::close(fd);
        throw std::runtime_error("indexed archive size or type changed");
    }
    return fd;
}

}  // namespace

void verifyOriginalArchive(const StoragePaths& storage, const SegmentFileRecord& file) {
    const int fd = openVerified(storage, file);
    Sha256 sha256;
    char buffer[65536];
    std::uint64_t size = 0;
    for (;;) {
        const ssize_t count = ::read(fd, buffer, sizeof(buffer));
        if (count == 0) {
            break;
        }
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            const int saved_errno = errno;
            ::close(fd);
            throw std::runtime_error("cannot verify indexed archive: " +
                                     std::string(std::strerror(saved_errno)));
        }
        sha256.update(buffer, static_cast<std::size_t>(count));
        size += static_cast<std::uint64_t>(count);
    }
    if (::close(fd) != 0) {
        throw std::runtime_error("cannot close indexed archive: " +
                                 std::string(std::strerror(errno)));
    }
    if (size != file.file_size || sha256.finishHex() != file.file_sha256) {
        throw std::runtime_error("indexed archive integrity changed after parsing");
    }
}

std::string readOriginalRecord(const StoragePaths& storage, const SegmentFileRecord& file,
                               std::uint64_t byte_offset, std::uint64_t byte_length) {
    if (byte_length == 0 || byte_offset > file.file_size ||
        byte_length > file.file_size - byte_offset ||
        byte_length > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        byte_offset > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
        throw std::runtime_error("indexed document byte range is invalid");
    }
    const int fd = openVerified(storage, file);
    std::string content(static_cast<std::size_t>(byte_length), '\0');
    std::size_t completed = 0;
    while (completed < content.size()) {
        const std::uint64_t current_offset = byte_offset + completed;
        if (current_offset > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
            ::close(fd);
            throw std::runtime_error("indexed document byte offset exceeds platform limit");
        }
        const ssize_t count = ::pread(fd, &content[completed], content.size() - completed,
                                      static_cast<off_t>(current_offset));
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            const int saved_errno = errno;
            ::close(fd);
            throw std::runtime_error("cannot pread indexed archive: " +
                                     std::string(std::strerror(saved_errno)));
        }
        if (count == 0) {
            ::close(fd);
            throw std::runtime_error("indexed archive was truncated during pread");
        }
        completed += static_cast<std::size_t>(count);
    }
    if (::close(fd) != 0) {
        throw std::runtime_error("cannot close indexed archive: " +
                                 std::string(std::strerror(errno)));
    }
    return content;
}

}  // namespace logtrace
}  // namespace smt
