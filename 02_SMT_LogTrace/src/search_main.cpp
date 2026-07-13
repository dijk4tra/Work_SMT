/**
 * @file search_main.cpp
 * @brief Search Server 进程入口。
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
#include "logtrace/server/logsearch_server.h"

namespace {

std::string parseConfigPath(int argc, char* argv[]) {
    if (argc != 3 || std::string(argv[1]) != "--config" || argv[2][0] == '\0') {
        throw std::invalid_argument("usage: logsearch_server --config <path>");
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
        smt::logtrace::initializeLogging(config.logging, config.logging.search_file,
                                         "logsearch_server");
        const sigset_t shutdown_signals = blockShutdownSignals();

        smt::logtrace::LogSearchServer server(config);
        server.initialize();
        if (!server.start()) {
            throw std::runtime_error(std::string("SRPC server start failed: ") +
                                     std::strerror(errno));
        }

        spdlog::info("event=search_server_started address={} port={}",
                     config.search_rpc.listen_address, config.search_rpc.port);

        int signal_number = 0;
        const int wait_result = ::sigwait(&shutdown_signals, &signal_number);
        if (wait_result != 0) {
            throw std::runtime_error(std::string("cannot wait for shutdown signal: ") +
                                     std::strerror(wait_result));
        }

        spdlog::info("event=search_server_stopping signal={}", signal_number);
        server.stop();
        spdlog::info("event=search_server_stopped");
        spdlog::shutdown();
        google::protobuf::ShutdownProtobufLibrary();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "logsearch_server: " << error.what() << '\n';
        spdlog::shutdown();
        google::protobuf::ShutdownProtobufLibrary();
        return 1;
    }
}
