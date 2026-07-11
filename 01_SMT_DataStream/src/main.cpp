/**
 * @file main.cpp
 * @brief 数据采集与归档服务进程入口。
 */

#include <pthread.h>
#include <spdlog/spdlog.h>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

#include "datastream/common/logging.h"
#include "datastream/config/app_config.h"
#include "datastream/server/datastream_server.h"

namespace {

std::string parseConfigPath(int argc, char* argv[]) {
    if (argc != 3 || std::string(argv[1]) != "--config" || argv[2][0] == '\0') {
        throw std::invalid_argument("usage: datastream_server --config <path>");
    }
    return argv[2];
}

sigset_t blockShutdownSignals() {
    sigset_t signals;
    ::sigemptyset(&signals);
    ::sigaddset(&signals, SIGINT);
    ::sigaddset(&signals, SIGTERM);
    const int result = ::pthread_sigmask(SIG_BLOCK, &signals, nullptr);
    if (result != 0) {
        throw std::runtime_error(std::string("cannot block shutdown signals: ") +
                                 std::strerror(result));
    }
    return signals;
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        const std::string config_path = parseConfigPath(argc, argv);
        const smt::datastream::AppConfig config = smt::datastream::AppConfig::load(config_path);
        smt::datastream::initializeLogging(config.logging);
        const sigset_t shutdown_signals = blockShutdownSignals();

        smt::datastream::DataStreamServer server(config);
        server.initialize();
        if (!server.start()) {
            throw std::runtime_error(std::string("HTTP server start failed: ") +
                                     std::strerror(errno));
        }

        spdlog::info("event=server_started address={} port={}", config.http.listen_address,
                     config.http.port);

        int signal_number = 0;
        const int wait_result = ::sigwait(&shutdown_signals, &signal_number);
        if (wait_result != 0) {
            throw std::runtime_error(std::string("cannot wait for shutdown signal: ") +
                                     std::strerror(wait_result));
        }

        spdlog::info("event=server_stopping signal={}", signal_number);
        server.stop();
        spdlog::info("event=server_stopped");
        spdlog::shutdown();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "datastream_server: " << error.what() << '\n';
        return 1;
    }
}
