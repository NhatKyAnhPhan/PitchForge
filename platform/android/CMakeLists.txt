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
    $<TARGET_OBJECTS:auralis_audio_engine_common>
)

if(NOT TARGET auralis_audio_engine_common
    OR NOT TARGET auralis_mixer
    OR NOT TARGET auralis_dsp)
    message(FATAL_ERROR
        "The Android audio engine requires the common engine, mixer, and DSP targets.")
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
        auralis_mixer
        auralis_dsp
        oboe::oboe
        ${android-lib}
        ${log-lib}
)

target_link_options(auralis_audio_engine PRIVATE
    "-Wl,--no-undefined"
)
