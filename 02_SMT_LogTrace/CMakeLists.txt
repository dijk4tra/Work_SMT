cmake_minimum_required(VERSION 3.16)
project(SMTLogTrace VERSION 0.1.0 LANGUAGES CXX)

option(LOGTRACE_BUILD_TESTS "Build unit tests" ON)
option(LOGTRACE_ENABLE_INTEGRATION_TESTS "Build tests that connect to local services" OFF)
option(LOGTRACE_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(LOGTRACE_ENABLE_COVERAGE "Enable GCC coverage instrumentation" OFF)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(OpenSSL REQUIRED)
find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(spdlog REQUIRED)
find_package(srpc REQUIRED CONFIG PATHS /usr/local/lib/cmake/srpc)

find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp PATHS /usr/local/include /usr/include REQUIRED)
find_path(WORKFLOW_INCLUDE_DIR workflow/WFTaskFactory.h PATHS /usr/local/include /usr/include REQUIRED)
find_library(WORKFLOW_LIBRARY workflow PATHS /usr/local/lib /usr/lib REQUIRED)
find_path(WFREST_INCLUDE_DIR wfrest/HttpServer.h PATHS /usr/local/include /usr/include REQUIRED)
find_library(WFREST_LIBRARY wfrest PATHS /usr/local/lib /usr/lib REQUIRED)
find_library(SRPC_LIBRARY srpc PATHS ${SRPC_LIB_DIR} /usr/local/lib /usr/lib REQUIRED)
find_library(SNAPPY_LIBRARY snappy PATHS /usr/local/lib /usr/lib /usr/lib/x86_64-linux-gnu REQUIRED)
find_library(LZ4_LIBRARY lz4 PATHS /usr/local/lib /usr/lib /usr/lib/x86_64-linux-gnu REQUIRED)
find_program(SRPC_GENERATOR srpc_generator PATHS ${SRPC_BIN_DIR} /usr/local/bin REQUIRED)

set(LOGTRACE_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${LOGTRACE_GENERATED_DIR})

set(LOGTRACE_PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/proto/logtrace.proto)
set(LOGTRACE_PROTO_SOURCE ${LOGTRACE_GENERATED_DIR}/logtrace.pb.cc)
set(LOGTRACE_PROTO_HEADER ${LOGTRACE_GENERATED_DIR}/logtrace.pb.h)
set(LOGTRACE_SRPC_HEADER ${LOGTRACE_GENERATED_DIR}/logtrace.srpc.h)

add_custom_command(
    OUTPUT ${LOGTRACE_PROTO_SOURCE} ${LOGTRACE_PROTO_HEADER} ${LOGTRACE_SRPC_HEADER}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
            --cpp_out=${LOGTRACE_GENERATED_DIR}
            -I ${CMAKE_CURRENT_SOURCE_DIR}/proto
            ${LOGTRACE_PROTO_FILE}
    COMMAND ${SRPC_GENERATOR}
            protobuf -s -f ${LOGTRACE_PROTO_FILE} -o ${LOGTRACE_GENERATED_DIR}
    DEPENDS ${LOGTRACE_PROTO_FILE}
    COMMENT "Generating Protobuf and SRPC sources"
    VERBATIM
)

add_custom_target(logtrace_rpc_codegen
    DEPENDS ${LOGTRACE_PROTO_SOURCE} ${LOGTRACE_PROTO_HEADER} ${LOGTRACE_SRPC_HEADER})

add_library(logtrace_proto STATIC ${LOGTRACE_PROTO_SOURCE})
add_dependencies(logtrace_proto logtrace_rpc_codegen)
target_include_directories(logtrace_proto SYSTEM PUBLIC ${LOGTRACE_GENERATED_DIR})
target_link_libraries(logtrace_proto PUBLIC protobuf::libprotobuf ${SRPC_LIBRARY})

set(LOGTRACE_CORE_SOURCES
    src/api/health_controller.cpp
    src/common/api_response.cpp
    src/common/logging.cpp
    src/common/request_id.cpp
    src/common/uri_utils.cpp
    src/config/app_config.cpp
    src/rpc/search_health_service.cpp
    src/rpc/search_rpc_client.cpp
    src/server/logsearch_server.cpp
    src/server/logtrace_gateway.cpp
    src/storage/mysql_client.cpp
    src/storage/redis_client.cpp
    src/storage/storage_paths.cpp
)

add_library(logtrace_core STATIC ${LOGTRACE_CORE_SOURCES})
add_dependencies(logtrace_core logtrace_rpc_codegen)
target_include_directories(
    logtrace_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${NLOHMANN_JSON_INCLUDE_DIR}
        ${WORKFLOW_INCLUDE_DIR}
        ${WFREST_INCLUDE_DIR}
    SYSTEM PUBLIC
        ${LOGTRACE_GENERATED_DIR}
        ${SRPC_INCLUDE_DIR}
        ${Protobuf_INCLUDE_DIRS}
)
target_link_libraries(
    logtrace_core
    PUBLIC
        logtrace_proto
        ${WFREST_LIBRARY}
        ${WORKFLOW_LIBRARY}
        ${SRPC_LIBRARY}
        ZLIB::ZLIB
        ${SNAPPY_LIBRARY}
        ${LZ4_LIBRARY}
        OpenSSL::SSL
        OpenSSL::Crypto
        spdlog::spdlog
        Threads::Threads
)
target_compile_options(logtrace_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(LOGTRACE_ENABLE_SANITIZERS)
    target_compile_options(logtrace_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(logtrace_core PUBLIC -fsanitize=address,undefined)
endif()

if(LOGTRACE_ENABLE_COVERAGE)
    target_compile_options(logtrace_core PUBLIC --coverage -O0 -g)
    target_link_options(logtrace_core PUBLIC --coverage)
endif()

add_executable(logsearch_server src/search_main.cpp)
target_link_libraries(logsearch_server PRIVATE logtrace_core)
target_compile_options(logsearch_server PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(logtrace_gateway src/gateway_main.cpp)
target_link_libraries(logtrace_gateway PRIVATE logtrace_core)
target_compile_options(logtrace_gateway PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(LOGTRACE_BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    add_subdirectory(tests)
endif()
