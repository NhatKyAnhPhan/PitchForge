cmake_minimum_required(VERSION 3.22)

project(AuralisDspModuleTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

add_subdirectory(
    "${CMAKE_CURRENT_LIST_DIR}/../../native/src/dsp"
    "${CMAKE_CURRENT_BINARY_DIR}/auralis_dsp"
)

add_executable(auralis_dsp_tests DspTests.cpp)
target_link_libraries(auralis_dsp_tests PRIVATE auralis_dsp)
target_include_directories(auralis_dsp_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../native/src/dsp"
)

if(MSVC)
    target_compile_options(auralis_dsp_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(auralis_dsp_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME auralis_dsp_tests COMMAND auralis_dsp_tests)
