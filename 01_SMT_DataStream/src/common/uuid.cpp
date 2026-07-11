/**
 * @file uuid.cpp
 * @brief 实现 OpenSSL 随机源生成和校验 UUIDv4。
 */

#include "datastream/common/uuid.h"

#include <openssl/rand.h>

#include <cstdio>

namespace smt {
namespace datastream {

RandomError::RandomError(const std::string& message) : std::runtime_error(message) {}

std::string generateUuidV4() {
    unsigned char bytes[16];
    if (RAND_bytes(bytes, sizeof(bytes)) != 1) {
        throw RandomError("cannot obtain random bytes for upload id");
    }
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0fU) | 0x40U);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3fU) | 0x80U);
    char output[37];
    std::snprintf(output, sizeof(output),
                  "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
                  "%02x%02x%02x%02x%02x%02x",
                  bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
                  bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14],
                  bytes[15]);
    return output;
}

bool isUuid(const std::string& value) {
    if (value.size() != 36) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 8 || index == 13 || index == 18 || index == 23) {
            if (value[index] != '-') {
                return false;
            }
        } else if (!((value[index] >= '0' && value[index] <= '9') ||
                     (value[index] >= 'a' && value[index] <= 'f'))) {
            return false;
        }
    }
    return true;
}

}  // namespace datastream
}  // namespace smt
