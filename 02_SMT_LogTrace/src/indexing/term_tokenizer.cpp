/**
 * @file term_tokenizer.cpp
 * @brief 实现保留设备号和错误码字符的正文词项规范化。
 */

#include "logtrace/indexing/term_tokenizer.h"

#include <cstddef>

namespace smt {
namespace logtrace {

std::vector<std::string> tokenizeTerms(const std::string& text) {
    std::vector<std::string> terms;
    std::string current;
    for (std::string::const_iterator it = text.begin(); it != text.end(); ++it) {
        const unsigned char value = static_cast<unsigned char>(*it);
        const bool term_character =
            (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
            (value >= '0' && value <= '9') || value == '_' || value == '-' || value >= 0x80;
        if (!term_character) {
            if (!current.empty()) {
                terms.push_back(current);
                current.clear();
            }
            continue;
        }
        if (value >= 'A' && value <= 'Z') {
            current.push_back(static_cast<char>(value - 'A' + 'a'));
        } else {
            current.push_back(*it);
        }
    }
    if (!current.empty()) {
        terms.push_back(current);
    }
    return terms;
}

}  // namespace logtrace
}  // namespace smt
