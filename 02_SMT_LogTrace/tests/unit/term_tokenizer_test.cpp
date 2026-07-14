/**
 * @file term_tokenizer_test.cpp
 * @brief 验证索引与后续查询共用的词项规范化规则。
 */

#include "logtrace/indexing/term_tokenizer.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace smt {
namespace logtrace {
namespace {

TEST(TermTokenizerTest, LowercasesAsciiAndPreservesIndustrialIdentifiers) {
    const std::vector<std::string> terms =
        tokenizeTerms("Camera TIMEOUT code=INSPECTION_NG device=AOI-VT-01 detail=相机超时");
    const std::vector<std::string> expected = {"camera", "timeout",   "code",   "inspection_ng",
                                               "device", "aoi-vt-01", "detail", "相机超时"};
    EXPECT_EQ(terms, expected);
}

TEST(TermTokenizerTest, KeepsDuplicateTermsForDocumentLengthAndFrequency) {
    const std::vector<std::string> terms = tokenizeTerms("alarm alarm ALARM");
    ASSERT_EQ(terms.size(), 3U);
    EXPECT_EQ(terms[0], "alarm");
    EXPECT_EQ(terms[1], "alarm");
    EXPECT_EQ(terms[2], "alarm");
}

}  // namespace
}  // namespace logtrace
}  // namespace smt
