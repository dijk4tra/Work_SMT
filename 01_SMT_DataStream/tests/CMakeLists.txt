include(GoogleTest)

add_executable(
    datastream_unit_tests
    unit/app_config_test.cpp
    unit/api_response_test.cpp
    unit/storage_paths_test.cpp
    unit/uri_utils_test.cpp
)
target_link_libraries(datastream_unit_tests PRIVATE datastream_core GTest::gtest_main)
target_compile_options(datastream_unit_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_definitions(
    datastream_unit_tests
    PRIVATE DATASTREAM_TEST_CONFIG_PATH="${CMAKE_SOURCE_DIR}/conf/datastream.example.json"
)
gtest_discover_tests(datastream_unit_tests DISCOVERY_MODE PRE_TEST)

if(DATASTREAM_ENABLE_INTEGRATION_TESTS)
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
endif()
