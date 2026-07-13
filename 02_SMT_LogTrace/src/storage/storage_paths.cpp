/**
 * @file storage_paths.cpp
 * @brief 实现归档只读目录和索引可写目录检查。
 */

#include "logtrace/storage/storage_paths.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace smt {
namespace logtrace {
namespace {

bool isDirectory(const std::string& path) {
    struct stat info;
    return ::stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

void createDirectories(const std::string& path) {
    std::string current;
    if (!path.empty() && path[0] == '/') {
        current = "/";
    }

    std::size_t begin = 0;
    while (begin < path.size()) {
        const std::size_t end = path.find('/', begin);
        const std::string part =
            path.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        begin = end == std::string::npos ? path.size() : end + 1;
        if (part.empty() || part == ".") {
            continue;
        }

        if (!current.empty() && current[current.size() - 1] != '/') {
            current += '/';
        }
        current += part;

        if (::mkdir(current.c_str(), 0750) != 0 && errno != EEXIST) {
            throw std::runtime_error("cannot create directory " + current + ": " +
                                     std::strerror(errno));
        }
        if (!isDirectory(current)) {
            throw std::runtime_error("path is not a directory: " + current);
        }
    }
}

void verifyWritable(const std::string& path) {
    const std::string probe = path + "/.logtrace_write_probe";
    const int fd = ::open(probe.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) {
        throw std::runtime_error("index directory is not writable: " + path + ": " +
                                 std::strerror(errno));
    }
    if (::close(fd) != 0) {
        const int saved_errno = errno;
        ::unlink(probe.c_str());
        throw std::runtime_error("cannot close index directory probe: " +
                                 std::string(std::strerror(saved_errno)));
    }
    if (::unlink(probe.c_str()) != 0) {
        throw std::runtime_error("cannot remove index directory probe: " +
                                 std::string(std::strerror(errno)));
    }
}

bool validRelativePath(const std::string& path) {
    if (path.empty() || path[0] == '/') {
        return false;
    }
    std::size_t begin = 0;
    while (begin <= path.size()) {
        const std::size_t end = path.find('/', begin);
        const std::string part =
            path.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    return true;
}

std::string canonicalRoot(const std::string& path) {
    std::vector<char> buffer(PATH_MAX + 1, '\0');
    if (::realpath(path.c_str(), buffer.data()) == nullptr) {
        throw std::runtime_error("cannot resolve path " + path + ": " + std::strerror(errno));
    }
    return buffer.data();
}

}  // namespace

ArchivePathError::ArchivePathError(const char* code, const std::string& message)
    : std::runtime_error(message), code_(code) {}

const std::string& ArchivePathError::code() const { return code_; }

StoragePaths::StoragePaths(const StorageConfig& config)
    : archive_root_(config.archive_root), index_root_(config.index_root) {}

void StoragePaths::initialize() {
    if (!isDirectory(archive_root_)) {
        throw std::runtime_error("archive root is not an existing directory: " + archive_root_);
    }
    if (::access(archive_root_.c_str(), R_OK | X_OK) != 0) {
        throw std::runtime_error("archive root is not readable: " + archive_root_);
    }

    createDirectories(index_root_);
    verifyWritable(index_root_);
}

bool StoragePaths::ready() const {
    return isDirectory(archive_root_) && isDirectory(index_root_) &&
           ::access(archive_root_.c_str(), R_OK | X_OK) == 0 &&
           ::access(index_root_.c_str(), W_OK | X_OK) == 0;
}

const std::string& StoragePaths::archiveRoot() const { return archive_root_; }

const std::string& StoragePaths::indexRoot() const { return index_root_; }

std::string StoragePaths::resolveArchiveFile(const std::string& relative_path) const {
    if (!validRelativePath(relative_path)) {
        throw ArchivePathError("ARCHIVE_PATH_INVALID", "archive relative path is invalid");
    }
    const std::string root = canonicalRoot(archive_root_);
    std::vector<char> buffer(PATH_MAX + 1, '\0');
    const std::string unresolved = archive_root_ + "/" + relative_path;
    if (::realpath(unresolved.c_str(), buffer.data()) == nullptr) {
        const char* code = errno == ENOENT ? "ARCHIVE_FILE_NOT_FOUND" : "ARCHIVE_PATH_INVALID";
        throw ArchivePathError(code, "cannot resolve archive file path");
    }
    std::string candidate = buffer.data();
    const std::string prefix = root + "/";
    if (candidate.compare(0, prefix.size(), prefix) != 0) {
        throw ArchivePathError("ARCHIVE_PATH_INVALID", "archive path escapes archive root");
    }
    struct stat info;
    if (::stat(candidate.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) {
        throw ArchivePathError("ARCHIVE_FILE_NOT_REGULAR", "archive path is not a regular file");
    }
    return candidate;
}

}  // namespace logtrace
}  // namespace smt
