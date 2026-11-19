cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(AuralisAudioEngine LANGUAGES C CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    get_filename_component(
        AURALIS_REPOSITORY_ROOT
        "${CMAKE_CURRENT_SOURCE_DIR}/../../.."
        ABSOLUTE
    )
endif()

if(NOT DEFINED AURALIS_REPOSITORY_ROOT)
    set(AURALIS_REPOSITORY_ROOT "${PROJECT_SOURCE_DIR}")
endif()

if(NOT TARGET auralis_core)
    add_library(auralis_core INTERFACE)
    target_include_directories(
        auralis_core
        INTERFACE "${AURALIS_REPOSITORY_ROOT}/include"
    )
    target_compile_features(auralis_core INTERFACE cxx_std_20)
endif()

if(NOT DEFINED AURALIS_FETCH_JUCE)
    option(AURALIS_FETCH_JUCE "Fetch pinned JUCE 8.0.11" OFF)
endif()

if(AURALIS_FETCH_JUCE AND NOT TARGET juce::juce_audio_devices)
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.11
        GIT_SHALLOW TRUE
    )
    set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
    set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(JUCE)
endif()

add_library(auralis_audio_engine_common OBJECT AudioEngine.cpp)
target_include_directories(
    auralis_audio_engine_common
    PRIVATE
        "${AURALIS_REPOSITORY_ROOT}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_features(auralis_audio_engine_common PRIVATE cxx_std_20)
target_compile_definitions(
    auralis_audio_engine_common
    PRIVATE AURALIS_BUILDING_SHARED
)

if(WIN32 AND TARGET juce::juce_audio_devices)
    add_library(
        auralis_audio_engine_windows_backend
        OBJECT ../platform/windows/JuceAudioDeviceBackend.cpp
    )
    target_include_directories(
        auralis_audio_engine_windows_backend
        PRIVATE "${AURALIS_REPOSITORY_ROOT}/include"
    )
    target_compile_features(
        auralis_audio_engine_windows_backend
        PRIVATE cxx_std_20
    )
    target_compile_definitions(
        auralis_audio_engine_windows_backend
        PRIVATE JUCE_ASIO=0 JUCE_WASAPI=1
    )
    target_link_libraries(
        auralis_audio_engine_windows_backend
        PRIVATE juce::juce_audio_devices
    )

    if(TARGET auralis_mixer AND TARGET auralis_dsp)
        add_library(
            auralis_audio_engine
            SHARED
            $<TARGET_OBJECTS:auralis_audio_engine_common>
            $<TARGET_OBJECTS:auralis_audio_engine_windows_backend>
        )
        target_include_directories(
            auralis_audio_engine
            PUBLIC "${AURALIS_REPOSITORY_ROOT}/include"
        )
        target_compile_features(auralis_audio_engine PRIVATE cxx_std_20)
        target_compile_definitions(
            auralis_audio_engine
            PRIVATE AURALIS_BUILDING_SHARED JUCE_ASIO=0 JUCE_WASAPI=1
        )
        target_link_libraries(
            auralis_audio_engine
            PRIVATE
                auralis_core
                auralis_mixer
                auralis_dsp
                juce::juce_audio_devices
        )
        set_target_properties(
            auralis_audio_engine
            PROPERTIES
                OUTPUT_NAME auralis_audio_engine
                WINDOWS_EXPORT_ALL_SYMBOLS OFF
        )
    else()
        message(STATUS
            "The Audio Engine DLL is deferred until auralis_mixer and "
            "auralis_dsp targets are available."
        )
    endif()
elseif(WIN32)
    message(STATUS
        "The Windows backend requires AURALIS_FETCH_JUCE=ON "
        "(pinned JUCE 8.0.11)."
    )
endif()
