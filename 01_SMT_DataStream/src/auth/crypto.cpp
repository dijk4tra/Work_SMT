/**
 * @file crypto.cpp
 * @brief 实现设备认证使用的密码学基础操作。
 */

#include "datastream/auth/crypto.h"

#include <openssl/crypto.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

#include <iomanip>
#include <sstream>

namespace smt {
namespace datastream {
namespace {

std::string toHex(const unsigned char* bytes, std::size_t size) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < size; ++index) {
        output << std::setw(2) << static_cast<unsigned int>(bytes[index]);
    }
    return output.str();
}

}  // namespace

std::string sha256Hex(const std::string& data) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), digest);
    return toHex(digest, SHA256_DIGEST_LENGTH);
}

std::string hmacSha256Hex(const std::string& key, const std::string& data) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_size = 0;
    HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(data.data()), data.size(), digest, &digest_size);
    return toHex(digest, digest_size);
}

bool constantTimeEquals(const std::string& left, const std::string& right) {
    return left.size() == right.size() &&
           CRYPTO_memcmp(left.data(), right.data(), left.size()) == 0;
}

std::string buildDeviceCanonicalString(const std::string& method, const std::string& path,
                                       const std::string& device_id, const std::string& timestamp,
                                       const std::string& request_id,
                                       const std::string& content_sha256) {
    return "v1\n" + method + "\n" + path + "\n" + device_id + "\n" + timestamp + "\n" + request_id +
           "\n" + content_sha256;
}

}  // namespace datastream
}  // namespace smt
