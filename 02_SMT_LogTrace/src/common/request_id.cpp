/**
 * @file request_id.cpp
 * @brief 实现基于 OpenSSL 随机源的请求标识生成。
 */

#include "logtrace/common/request_id.h"

#include <openssl/rand.h>

#include <stdexcept>

namespace smt {
namespace logtrace {

std::string generateRequestId() {
    unsigned char bytes[16];
    if (RAND_bytes(bytes, sizeof(bytes)) != 1) {
        throw std::runtime_error("cannot generate request id");
    }

    static const char kHex[] = "0123456789abcdef";
    std::string result(sizeof(bytes) * 2, '0');
    for (std::size_t index = 0; index < sizeof(bytes); ++index) {
        result[index * 2] = kHex[bytes[index] >> 4];
        result[index * 2 + 1] = kHex[bytes[index] & 0x0F];
    }
    return result;
}

}  // namespace logtrace
}  // namespace smt
