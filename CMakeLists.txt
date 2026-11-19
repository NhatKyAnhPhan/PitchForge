cmake_minimum_required(VERSION 3.22)

project(AuralisStudio
    VERSION 0.1.0
    DESCRIPTION "Realtime vocal processing for Windows and Android"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AURALIS_BUILD_TESTS "Build Auralis native unit tests" ON)
option(AURALIS_FETCH_JUCE "Fetch the pinned JUCE dependency" OFF)

if(AURALIS_FETCH_JUCE)
    if(ANDROID)
        message(FATAL_ERROR
            "JUCE 8.0.11 does not support Android app targets through its official CMake API. "
            "Build Android via Flutter/Gradle and platform/android with Oboe.")
    endif()

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

add_subdirectory(native)

if(AURALIS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
