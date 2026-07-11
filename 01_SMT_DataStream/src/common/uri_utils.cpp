/**
 * @file uri_utils.cpp
 * @brief 实现连接 URI 用户信息的百分号编码。
 */

#include "datastream/common/uri_utils.h"

#include <iomanip>
#include <sstream>

namespace smt {
namespace datastream {

std::string encodeUriUserInfo(const std::string& value) {
    std::ostringstream output;
    output << std::uppercase << std::hex;
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it) {
        const unsigned char c = static_cast<unsigned char>(*it);
        const bool unreserved = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||
                                c == '~';
        if (unreserved) {
            output << static_cast<char>(c);
        } else {
            output << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
        }
    }
    return output.str();
}

}  // namespace datastream
}  // namespace smt
