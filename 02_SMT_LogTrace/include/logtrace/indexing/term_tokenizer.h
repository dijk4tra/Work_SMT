/**
 * @file term_tokenizer.h
 * @brief 声明索引构建和后续查询共用的正文词项规范化函数。
 */

#ifndef LOGTRACE_INDEXING_TERM_TOKENIZER_H_
#define LOGTRACE_INDEXING_TERM_TOKENIZER_H_

#include <string>
#include <vector>

namespace smt {
namespace logtrace {

/// @brief 将 UTF-8 正文切分为保序词项，并把 ASCII 字母转换为小写。
/// @param text 已校验的 UTF-8 原始记录。
/// @return 包含重复词项的规范化序列。
std::vector<std::string> tokenizeTerms(const std::string& text);

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_INDEXING_TERM_TOKENIZER_H_
