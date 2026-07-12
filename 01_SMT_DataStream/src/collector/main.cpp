/**
 * @file main.cpp
 * @brief 参考采集程序进程入口。
 */

#include <pthread.h>

#include <atomic>
#include <csignal>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include "datastream/collector/collector_app.h"
#include "datastream/collector/collector_config.h"

namespace {

std::string configPath(int argc, char* argv[]) {
    if (argc != 3 || std::string(argv[1]) != "--config" || argv[2][0] == '\0') {
        throw std::invalid_argument("usage: collector_agent --config <path>");
    }
    return argv[2];
}

sigset_t blockSignals() {
    sigset_t signals;
    ::sigemptyset(&signals);
    ::sigaddset(&signals, SIGINT);
    ::sigaddset(&signals, SIGTERM);
    const int result = ::pthread_sigmask(SIG_BLOCK, &signals, nullptr);
    if (result != 0) throw std::runtime_error(std::strerror(result));
    return signals;
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        const smt::datastream::CollectorConfig config =
            smt::datastream::CollectorConfig::load(configPath(argc, argv));
        const sigset_t signals = blockSignals();
        smt::datastream::CollectorApp app(config);
        app.initialize();
        std::atomic<bool> stop(false);
        std::thread signal_thread([&signals, &stop]() {
            int signal_number = 0;
            if (::sigwait(&signals, &signal_number) == 0) {
                stop.store(true, std::memory_order_release);
            }
        });
        app.run(stop);
        signal_thread.join();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "collector_agent: " << error.what() << '\n';
        return 1;
    }
}
