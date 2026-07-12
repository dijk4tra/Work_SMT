include(GoogleTest)

add_executable(
    datastream_unit_tests
    unit/app_config_test.cpp
    unit/api_response_test.cpp
    unit/archive_cursor_test.cpp
    unit/archive_storage_test.cpp
    unit/cleanup_service_test.cpp
    unit/crypto_test.cpp
    unit/heartbeat_test.cpp
    unit/operator_authenticator_test.cpp
    unit/storage_paths_test.cpp
    unit/time_utils_test.cpp
    unit/upload_model_test.cpp
    unit/upload_repository_test.cpp
    unit/uri_utils_test.cpp
    unit/uuid_test.cpp
)
target_link_libraries(datastream_unit_tests PRIVATE datastream_core GTest::gtest_main)
target_compile_options(datastream_unit_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_definitions(
    datastream_unit_tests
    PRIVATE DATASTREAM_TEST_CONFIG_PATH="${CMAKE_SOURCE_DIR}/conf/datastream.example.json"
)
gtest_discover_tests(datastream_unit_tests DISCOVERY_MODE PRE_TEST)

if(DATASTREAM_ENABLE_INTEGRATION_TESTS)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(datastream_integration_tests integration/clients_test.cpp)
    target_link_libraries(datastream_integration_tests PRIVATE datastream_core GTest::gtest_main)
    target_compile_options(datastream_integration_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_definitions(
        datastream_integration_tests
        PRIVATE DATASTREAM_TEST_CONFIG_PATH="${CMAKE_SOURCE_DIR}/conf/datastream.example.json"
    )
    gtest_discover_tests(
        datastream_integration_tests
        DISCOVERY_MODE PRE_TEST
        PROPERTIES LABELS integration
    )

    add_test(
        NAME HeartbeatE2ETest
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/e2e/heartbeat_e2e_test.py
            $<TARGET_FILE:datastream_server> ${CMAKE_SOURCE_DIR}/conf/datastream.example.json
    )
    set_tests_properties(HeartbeatE2ETest PROPERTIES LABELS integration TIMEOUT 30)

    add_test(
        NAME UploadE2ETest
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/e2e/upload_e2e_test.py
            $<TARGET_FILE:datastream_server> ${CMAKE_SOURCE_DIR}/conf/datastream.example.json
    )
    set_tests_properties(UploadE2ETest PROPERTIES LABELS integration TIMEOUT 60)

    add_test(
        NAME MockArchiveE2ETest
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/e2e/mock_archive_e2e_test.py
            $<TARGET_FILE:datastream_server> ${CMAKE_SOURCE_DIR}/conf/datastream.example.json
            ${CMAKE_SOURCE_DIR}/data/mock_inbox/run_20260711T142550760305Z_seed20260712
    )
    set_tests_properties(MockArchiveE2ETest PROPERTIES LABELS integration TIMEOUT 180)
endif()
