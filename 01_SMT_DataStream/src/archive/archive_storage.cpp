/**
 * @file archive_storage.cpp
 * @brief 实现窗口化 mmap SHA-256 校验和原子归档。
 */

#include "datastream/archive/archive_storage.h"

#include <fcntl.h>
#include <openssl/evp.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <iomanip>
#include <memory>
#include <sstream>

#include "datastream/common/time_utils.h"

namespace smt {
namespace datastream {
namespace {

bool ensureDirectory(const std::string& path) {
    struct stat info;
    if (::mkdir(path.c_str(), 0750) == 0) {
        return true;
    }
    return errno == EEXIST && ::stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool createArchiveDirectories(const std::string& root, const std::string& relative_path) {
    std::string current = root;
    std::size_t position = 0;
    while (true) {
        const std::size_t slash = relative_path.find('/', position);
        if (slash == std::string::npos) {
            return true;
        }
        current += "/" + relative_path.substr(position, slash - position);
        if (!ensureDirectory(current)) {
            return false;
        }
        position = slash + 1;
    }
}

std::string digestFile(const std::string& path, std::uint64_t expected_size,
                       std::size_t window_size, bool* io_ok, bool* size_ok) {
    *io_ok = false;
    *size_ok = false;
    const int descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        return "";
    }
    struct stat info;
    if (::fstat(descriptor, &info) != 0 || !S_ISREG(info.st_mode)) {
        ::close(descriptor);
        return "";
    }
    if (static_cast<std::uint64_t>(info.st_size) != expected_size) {
        *io_ok = true;
        ::close(descriptor);
        return "";
    }
    *size_ok = true;
    EVP_MD_CTX* raw_context = EVP_MD_CTX_new();
    if (raw_context == nullptr) {
        ::close(descriptor);
        return "";
    }
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(raw_context, EVP_MD_CTX_free);
    if (EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) {
        ::close(descriptor);
        return "";
    }
    const long page_size = ::sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        ::close(descriptor);
        return "";
    }
    std::uint64_t offset = 0;
    while (offset < expected_size) {
        const std::uint64_t data_length =
            std::min<std::uint64_t>(window_size, expected_size - offset);
        const std::uint64_t map_offset = offset - offset % static_cast<std::uint64_t>(page_size);
        const std::size_t prefix = static_cast<std::size_t>(offset - map_offset);
        const std::size_t map_length = prefix + static_cast<std::size_t>(data_length);
        void* mapping = ::mmap(nullptr, map_length, PROT_READ, MAP_PRIVATE, descriptor,
                               static_cast<off_t>(map_offset));
        if (mapping == MAP_FAILED) {
            ::close(descriptor);
            return "";
        }
        const unsigned char* data = static_cast<const unsigned char*>(mapping) + prefix;
        const int digest_result = EVP_DigestUpdate(context.get(), data, data_length);
        const int unmap_result = ::munmap(mapping, map_length);
        if (digest_result != 1 || unmap_result != 0) {
            ::close(descriptor);
            return "";
        }
        offset += data_length;
    }
    if (::close(descriptor) != 0) {
        return "";
    }
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_length = 0;
    if (EVP_DigestFinal_ex(context.get(), digest, &digest_length) != 1 || digest_length != 32) {
        return "";
    }
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (unsigned int index = 0; index < digest_length; ++index) {
        output << std::setw(2) << static_cast<unsigned int>(digest[index]);
    }
    *io_ok = true;
    return output.str();
}

}  // namespace

ArchiveStorageResult verifyAndArchiveFile(const StoragePaths& storage, const UploadSession& session,
                                          const std::string& relative,
                                          std::size_t mmap_window_bytes) {
    const std::string final_path = storage.archiveRoot() + "/" + relative;
    struct stat final_info;
    const bool final_exists = ::stat(final_path.c_str(), &final_info) == 0;
    const std::string source = final_exists ? final_path : session.temp_path;
    bool io_ok = false;
    bool size_ok = false;
    const std::string digest =
        digestFile(source, session.file_size, mmap_window_bytes, &io_ok, &size_ok);
    if (!io_ok) {
        return ArchiveStorageResult{ArchiveStorageStatus::IoError, relative};
    }
    if (!size_ok || digest != session.file_sha256) {
        return ArchiveStorageResult{ArchiveStorageStatus::IntegrityMismatch, relative};
    }
    if (!final_exists) {
        if (!createArchiveDirectories(storage.archiveRoot(), relative) ||
            ::rename(session.temp_path.c_str(), final_path.c_str()) != 0) {
            return ArchiveStorageResult{ArchiveStorageStatus::IoError, relative};
        }
    }
    return ArchiveStorageResult{ArchiveStorageStatus::Archived, relative};
}

std::string buildArchiveRelativePath(const UploadSession& session,
                                     std::int64_t archived_at_milliseconds) {
    const std::string date = formatUtcMilliseconds(archived_at_milliseconds);
    return date.substr(0, 4) + "/" + date.substr(5, 2) + "/" + date.substr(8, 2) + "/" +
           session.line_id + "/" + session.station_id + "/" + session.device_id + "/" +
           session.file_type + "/" + session.upload_id + "." + session.extension;
}

}  // namespace datastream
}  // namespace smt
