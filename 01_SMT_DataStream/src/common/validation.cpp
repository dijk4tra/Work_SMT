/**
 * @file validation.cpp
 * @brief 实现设备认证与业务模型共用的 SMT 标识校验。
 */

#include "datastream/common/validation.h"

#include <cctype>

namespace smt {
namespace datastream {

bool isSmtIdentifier(const std::string& value) {
    if (value.size() < 2 || value.size() > 64) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        const unsigned char character = static_cast<unsigned char>(value[index]);
        if (!(std::isupper(character) || std::isdigit(character) || character == '_' ||
              character == '-')) {
            return false;
        }
    }
    return true;
}

}  // namespace datastream
}  // namespace smt
