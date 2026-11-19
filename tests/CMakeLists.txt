add_executable(auralis_contract_tests
    native/AudioTypesTests.cpp
)

target_link_libraries(auralis_contract_tests PRIVATE auralis_core)

add_test(NAME auralis_contract_tests COMMAND auralis_contract_tests)

add_executable(auralis_c_api_header_test
    native/CAbiCompileTests.c
)

target_link_libraries(auralis_c_api_header_test PRIVATE auralis_core)

add_test(NAME auralis_c_api_header_test COMMAND auralis_c_api_header_test)
