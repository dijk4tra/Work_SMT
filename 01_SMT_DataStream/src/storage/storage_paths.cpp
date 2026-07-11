/**
 * @file storage_paths.cpp
 * @brief 实现上传和归档目录的创建及运行条件检查。
 */

#include "datastream/storage/storage_paths.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstring>
#include <sstream>

namespace smt {
namespace datastream {
namespace {

void ensureDirectory(const std::string& path) {
    struct stat info;
    if (::mkdir(path.c_str(), 0750) == 0) {
        return;
    }
    if (errno != EEXIST || ::stat(path.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) {
        throw StorageError("cannot create directory " + path + ": " + std::strerror(errno));
    }
}

void createDirectories(const std::string& path) {
    if (path.empty()) {
        throw StorageError("storage path must not be empty");
    }

    const bool absolute = path[0] == '/';
    std::string current = absolute ? "/" : "";
    std::stringstream stream(path);
    std::string component;
    while (std::getline(stream, component, '/')) {
        if (component.empty() || component == ".") {
            continue;
        }
        if (component == "..") {
            throw StorageError("storage path must not contain parent traversal");
        }

        if (current.empty()) {
            current = component;
        } else if (current == "/") {
            current += component;
        } else {
            current += "/" + component;
        }
        ensureDirectory(current);
    }

    if (current.empty()) {
        ensureDirectory(".");
    }
}

std::string canonicalPath(const std::string& path) {
    char resolved[PATH_MAX];
    if (::realpath(path.c_str(), resolved) == nullptr) {
        throw StorageError("cannot resolve storage path " + path + ": " + std::strerror(errno));
    }
    return resolved;
}

bool inspectDirectories(const std::string& temp_root, const std::string& archive_root) {
    struct stat temp_info;
    struct stat archive_info;
    return !temp_root.empty() && !archive_root.empty() &&
           ::stat(temp_root.c_str(), &temp_info) == 0 && S_ISDIR(temp_info.st_mode) &&
           ::stat(archive_root.c_str(), &archive_info) == 0 && S_ISDIR(archive_info.st_mode) &&
           temp_info.st_dev == archive_info.st_dev &&
           ::access(temp_root.c_str(), W_OK | X_OK) == 0 &&
           ::access(archive_root.c_str(), W_OK | X_OK) == 0;
}

}  // namespace

StorageError::StorageError(const std::string& message) : std::runtime_error(message) {}

StoragePaths::StoragePaths(const std::string& temp_root, const std::string& archive_root)
    : configured_temp_root_(temp_root), configured_archive_root_(archive_root) {}

void StoragePaths::initialize() {
    createDirectories(configured_temp_root_);
    createDirectories(configured_archive_root_);
    temp_root_ = canonicalPath(configured_temp_root_);
    archive_root_ = canonicalPath(configured_archive_root_);
    if (temp_root_ == archive_root_) {
        throw StorageError("temporary and archive directories resolve to the same path");
    }
    if (!inspectDirectories(temp_root_, archive_root_)) {
        throw StorageError("storage directories must be writable and on the same file system");
    }
}

bool StoragePaths::ready() const { return inspectDirectories(temp_root_, archive_root_); }

const std::string& StoragePaths::tempRoot() const { return temp_root_; }

const std::string& StoragePaths::archiveRoot() const { return archive_root_; }

}  // namespace datastream
}  // namespace smt
