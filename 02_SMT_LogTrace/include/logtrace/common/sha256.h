/**
 * @file sha256.h
 * @brief 声明流式 SHA-256 摘要计算工具。
 */

#ifndef LOGTRACE_COMMON_SHA256_H_
#define LOGTRACE_COMMON_SHA256_H_

#include <openssl/evp.h>

#include <cstddef>
#include <memory>
#include <string>

namespace smt {
namespace logtrace {

/// @brief 使用 OpenSSL EVP 按块计算 SHA-256。
class Sha256 {
   public:
    /// @brief 创建并初始化 SHA-256 上下文。
    /// @throws std::runtime_error 当 OpenSSL 初始化失败时抛出。
    Sha256();

    /// @brief 把一段原始字节加入摘要。
    /// @param data 字节起始地址。
    /// @param size 字节数。
    /// @throws std::runtime_error 当 OpenSSL 更新失败时抛出。
    void update(const void* data, std::size_t size);

    /// @brief 完成摘要并返回小写十六进制字符串。
    /// @return 64 字符 SHA-256。
    /// @throws std::runtime_error 当重复完成或 OpenSSL 结束失败时抛出。
    std::string finishHex();

   private:
    std::unique_ptr<EVP_MD_CTX, void (*)(EVP_MD_CTX*)> context_;
    bool finished_;
};

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_COMMON_SHA256_H_
