cmake_minimum_required(VERSION 3.16)
project(SMTDataStream VERSION 0.5.0 LANGUAGES CXX)

option(DATASTREAM_BUILD_TESTS "Build unit tests" ON)
option(DATASTREAM_ENABLE_INTEGRATION_TESTS "Build tests that connect to local services" OFF)
option(DATASTREAM_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(DATASTREAM_ENABLE_COVERAGE "Enable GCC coverage instrumentation" OFF)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(spdlog REQUIRED)

find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp PATHS /usr/local/include /usr/include REQUIRED)
find_path(WORKFLOW_INCLUDE_DIR workflow/WFTaskFactory.h PATHS /usr/local/include /usr/include REQUIRED)
find_library(WORKFLOW_LIBRARY workflow PATHS /usr/local/lib /usr/lib REQUIRED)
find_path(WFREST_INCLUDE_DIR wfrest/HttpServer.h PATHS /usr/local/include /usr/include REQUIRED)
find_library(WFREST_LIBRARY wfrest PATHS /usr/local/lib /usr/lib REQUIRED)

set(DATASTREAM_SOURCES
    src/api/archive_controller.cpp
    src/archive/archive_model.cpp
    src/archive/archive_repository.cpp
    src/archive/archive_storage.cpp
    src/api/health_controller.cpp
    src/api/heartbeat_controller.cpp
    src/api/upload_controller.cpp
    src/auth/crypto.cpp
    src/auth/device_authenticator.cpp
    src/auth/operator_authenticator.cpp
    src/common/api_response.cpp
    src/common/archive_cursor.cpp
    src/common/logging.cpp
    src/common/request_id.cpp
    src/common/time_utils.cpp
    src/common/uri_utils.cpp
    src/common/validation.cpp
    src/common/uuid.cpp
    src/cleanup/cleanup_service.cpp
    src/config/app_config.cpp
    src/device/device_repository.cpp
    src/device/heartbeat.cpp
    src/server/datastream_server.cpp
    src/storage/mysql_client.cpp
    src/storage/redis_client.cpp
    src/storage/storage_paths.cpp
    src/upload/upload_model.cpp
    src/upload/upload_repository.cpp
)

add_library(datastream_core STATIC ${DATASTREAM_SOURCES})
target_include_directories(
    datastream_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${NLOHMANN_JSON_INCLUDE_DIR}
        ${WORKFLOW_INCLUDE_DIR}
        ${WFREST_INCLUDE_DIR}
)
target_link_libraries(
    datastream_core
    PUBLIC
        ${WFREST_LIBRARY}
        ${WORKFLOW_LIBRARY}
        OpenSSL::SSL
        OpenSSL::Crypto
        spdlog::spdlog
        Threads::Threads
)
target_compile_options(datastream_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(DATASTREAM_ENABLE_SANITIZERS)
    target_compile_options(datastream_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(datastream_core PUBLIC -fsanitize=address,undefined)
endif()

if(DATASTREAM_ENABLE_COVERAGE)
    target_compile_options(datastream_core PUBLIC --coverage -O0 -g)
    target_link_options(datastream_core PUBLIC --coverage)
endif()

add_executable(datastream_server src/main.cpp)
target_link_libraries(datastream_server PRIVATE datastream_core)
target_compile_options(datastream_server PRIVATE -Wall -Wextra -Wpedantic -Werror)

set(COLLECTOR_SOURCES
    src/collector/collector_app.cpp
    src/collector/collector_config.cpp
    src/collector/http_uploader.cpp
    src/collector/spool_store.cpp
)
add_library(datastream_collector_core STATIC ${COLLECTOR_SOURCES})
target_include_directories(datastream_collector_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(datastream_collector_core PUBLIC datastream_core)
target_compile_options(datastream_collector_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(collector_agent src/collector/main.cpp)
target_link_libraries(collector_agent PRIVATE datastream_collector_core)
target_compile_options(collector_agent PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(DATASTREAM_BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    add_subdirectory(tests)
endif()
