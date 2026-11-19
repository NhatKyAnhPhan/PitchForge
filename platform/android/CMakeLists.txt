cmake_minimum_required(VERSION 3.22.1)

project(AuralisAndroidAudio LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(oboe REQUIRED CONFIG)
find_library(android-lib android REQUIRED)
find_library(log-lib log REQUIRED)

get_filename_component(AURALIS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

add_subdirectory(
    "${AURALIS_ROOT}/native"
    "${CMAKE_CURRENT_BINARY_DIR}/auralis_native"
)

add_library(auralis_audio_engine SHARED
    AndroidAudioDeviceBackend.cpp
    AndroidAudioHost.cpp
)

get_target_property(AURALIS_CORE_TYPE auralis_core TYPE)
get_target_property(AURALIS_CORE_SOURCES auralis_core INTERFACE_SOURCES)
if(AURALIS_CORE_TYPE STREQUAL "INTERFACE_LIBRARY"
    AND NOT AURALIS_CORE_SOURCES)
    file(GLOB_RECURSE AURALIS_COMMON_SOURCES CONFIGURE_DEPENDS
        "${AURALIS_ROOT}/native/src/*.c"
        "${AURALIS_ROOT}/native/src/*.cc"
        "${AURALIS_ROOT}/native/src/*.cpp"
    )
    list(FILTER AURALIS_COMMON_SOURCES EXCLUDE REGEX "/native/src/platform/")
    if(AURALIS_COMMON_SOURCES)
        target_sources(auralis_audio_engine PRIVATE ${AURALIS_COMMON_SOURCES})
    else()
        message(WARNING
            "auralis_core is headers-only and native/src has no common engine "
            "sources; libauralis_audio_engine.so will not export the shared C ABI")
    endif()
endif()

target_include_directories(auralis_audio_engine
    PRIVATE
        "${AURALIS_ROOT}/include"
        "${CMAKE_CURRENT_LIST_DIR}"
)

target_compile_definitions(auralis_audio_engine
    PRIVATE
        AURALIS_BUILDING_SHARED
)

target_compile_options(auralis_audio_engine PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

target_link_libraries(auralis_audio_engine
    PRIVATE
        auralis_core
        oboe::oboe
        ${android-lib}
        ${log-lib}
)

target_link_options(auralis_audio_engine PRIVATE
    "-Wl,--no-undefined"
)
