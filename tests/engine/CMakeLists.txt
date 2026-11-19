cmake_minimum_required(VERSION 3.22)
project(AuralisEngineTests LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(
    AURALIS_REPOSITORY_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../.."
    ABSOLUTE
)

find_package(Threads REQUIRED)

add_executable(
    auralis_engine_tests
    EngineTests.cpp
    "${AURALIS_REPOSITORY_ROOT}/native/src/audio/AudioEngine.cpp"
)
target_include_directories(
    auralis_engine_tests
    PRIVATE "${AURALIS_REPOSITORY_ROOT}/include"
)
target_compile_features(auralis_engine_tests PRIVATE cxx_std_20)
target_compile_definitions(auralis_engine_tests PRIVATE AURALIS_BUILDING_SHARED)
target_link_libraries(auralis_engine_tests PRIVATE Threads::Threads)

enable_testing()
add_test(NAME auralis_engine_tests COMMAND auralis_engine_tests)
