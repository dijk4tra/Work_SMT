/**
 * @file sha256.cpp
 * @brief 实现流式 SHA-256 摘要计算。
 */

#include "logtrace/common/sha256.h"

#include <stdexcept>

namespace smt {
namespace logtrace {

Sha256::Sha256() : context_(EVP_MD_CTX_new(), EVP_MD_CTX_free), finished_(false) {
    if (!context_ || EVP_DigestInit_ex(context_.get(), EVP_sha256(), nullptr) != 1) {
        throw std::runtime_error("cannot initialize SHA-256 context");
    }
}

void Sha256::update(const void* data, std::size_t size) {
    if (finished_) {
        throw std::runtime_error("SHA-256 context is already finalized");
    }
    if (size != 0 && EVP_DigestUpdate(context_.get(), data, size) != 1) {
        throw std::runtime_error("cannot update SHA-256 context");
    }
}

std::string Sha256::finishHex() {
    if (finished_) {
        throw std::runtime_error("SHA-256 context is already finalized");
    }
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_size = 0;
    if (EVP_DigestFinal_ex(context_.get(), digest, &digest_size) != 1 || digest_size != 32) {
        throw std::runtime_error("cannot finalize SHA-256 context");
    }
    finished_ = true;

    static const char kHex[] = "0123456789abcdef";
    const std::size_t output_size = static_cast<std::size_t>(digest_size) * 2;
    std::string result(output_size, '0');
    for (unsigned int index = 0; index < digest_size; ++index) {
        const std::size_t output_index = static_cast<std::size_t>(index) * 2;
        result[output_index] = kHex[digest[index] >> 4];
        result[output_index + 1] = kHex[digest[index] & 0x0F];
    }
    return result;
}

}  // namespace logtrace
}  // namespace smt
