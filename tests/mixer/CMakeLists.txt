if(NOT TARGET auralis_mixer)
    message(FATAL_ERROR
        "Add native/src/mixer before tests/mixer to build mixer tests")
endif()

find_package(Threads REQUIRED)

add_executable(auralis_mixer_tests
    SignalChainTests.cpp
    TestProcessorFactory.cpp
)

target_include_directories(auralis_mixer_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/native/src/mixer
)

target_link_libraries(auralis_mixer_tests
    PRIVATE
        auralis_mixer
        Threads::Threads
)

add_test(NAME auralis_mixer_tests COMMAND auralis_mixer_tests)
