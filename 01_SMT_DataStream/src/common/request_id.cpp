/**
 * @file request_id.cpp
 * @brief 实现无外部失败路径的服务端请求标识生成。
 */

#include "datastream/common/request_id.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <sstream>

namespace smt {
namespace datastream {

std::string generateRequestId() {
    static std::atomic<std::uint64_t> sequence(0);
    const std::uint64_t timestamp =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count());
    const std::uint64_t current = sequence.fetch_add(1, std::memory_order_relaxed);

    std::ostringstream output;
    output << "srv-" << std::hex << std::setw(16) << std::setfill('0') << timestamp << '-'
           << std::setw(8) << current;
    return output.str();
}

}  // namespace datastream
}  // namespace smt
