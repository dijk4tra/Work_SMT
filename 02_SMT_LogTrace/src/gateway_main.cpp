/**
 * @file gateway_main.cpp
 * @brief LogTrace Gateway 进程入口。
 */

#include <pthread.h>
#include <spdlog/spdlog.h>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

#include "logtrace.pb.h"
#include "logtrace/common/logging.h"
#include "logtrace/config/app_config.h"
#include "logtrace/server/logtrace_gateway.h"

namespace {

std::string parseConfigPath(int argc, char* argv[]) {
    if (argc != 3 || std::string(argv[1]) != "--config" || argv[2][0] == '\0') {
        throw std::invalid_argument("usage: logtrace_gateway --config <path>");
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
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    try {
        const std::string config_path = parseConfigPath(argc, argv);
        const smt::logtrace::AppConfig config = smt::logtrace::AppConfig::load(config_path);
        smt::logtrace::initializeLogging(config.logging, config.logging.gateway_file,
                                         "logtrace_gateway");
        const sigset_t shutdown_signals = blockShutdownSignals();

        smt::logtrace::LogTraceGateway gateway(config);
        gateway.initialize();
        if (!gateway.start()) {
            throw std::runtime_error(std::string("HTTP server start failed: ") +
                                     std::strerror(errno));
        }

        spdlog::info("event=gateway_started address={} port={}", config.gateway.listen_address,
                     config.gateway.port);

        int signal_number = 0;
        const int wait_result = ::sigwait(&shutdown_signals, &signal_number);
        if (wait_result != 0) {
            throw std::runtime_error(std::string("cannot wait for shutdown signal: ") +
                                     std::strerror(wait_result));
        }

        spdlog::info("event=gateway_stopping signal={}", signal_number);
        gateway.stop();
        spdlog::info("event=gateway_stopped");
        spdlog::shutdown();
        google::protobuf::ShutdownProtobufLibrary();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "logtrace_gateway: " << error.what() << '\n';
        spdlog::shutdown();
        google::protobuf::ShutdownProtobufLibrary();
        return 1;
    }
}
