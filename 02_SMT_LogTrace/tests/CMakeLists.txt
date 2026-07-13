include(GoogleTest)

add_executable(
    logtrace_unit_tests
    unit/api_response_test.cpp
    unit/app_config_test.cpp
    unit/archive_parser_test.cpp
    unit/parsed_batch_writer_test.cpp
    unit/proto_contract_test.cpp
    unit/storage_paths_test.cpp
    unit/uri_utils_test.cpp
)
target_link_libraries(logtrace_unit_tests PRIVATE logtrace_core GTest::gtest_main)
target_compile_options(logtrace_unit_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_definitions(
    logtrace_unit_tests
    PRIVATE LOGTRACE_TEST_CONFIG_PATH="${CMAKE_SOURCE_DIR}/conf/logtrace.json"
)
gtest_discover_tests(logtrace_unit_tests DISCOVERY_MODE PRE_TEST)

if(LOGTRACE_ENABLE_INTEGRATION_TESTS)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    add_executable(logtrace_integration_tests integration/clients_test.cpp)
    target_link_libraries(logtrace_integration_tests PRIVATE logtrace_core GTest::gtest_main)
    target_compile_options(logtrace_integration_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_definitions(
        logtrace_integration_tests
        PRIVATE LOGTRACE_TEST_CONFIG_PATH="${CMAKE_SOURCE_DIR}/conf/logtrace.json"
    )
    gtest_discover_tests(
        logtrace_integration_tests
        DISCOVERY_MODE PRE_TEST
        PROPERTIES LABELS integration
    )

    add_test(
        NAME HealthE2ETest
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/e2e/health_e2e_test.py
            $<TARGET_FILE:logsearch_server> $<TARGET_FILE:logtrace_gateway>
            ${CMAKE_SOURCE_DIR}/conf/logtrace.json
    )
    set_tests_properties(HealthE2ETest PROPERTIES LABELS integration TIMEOUT 45)

    add_test(
        NAME IndexingE2ETest
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/e2e/indexing_e2e_test.py
            $<TARGET_FILE:logtrace_admin> $<TARGET_FILE:logsearch_server>
            ${CMAKE_SOURCE_DIR}/conf/logtrace.json
            ${CMAKE_SOURCE_DIR}/tools/generate_phase2_samples.py
    )
    set_tests_properties(IndexingE2ETest PROPERTIES LABELS integration TIMEOUT 60)
endif()
