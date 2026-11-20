add_library(auralis_mixer STATIC
    SignalChain.cpp
    PresetCodec.cpp
)

find_package(Threads REQUIRED)

target_link_libraries(auralis_mixer
    PUBLIC
        auralis_dsp
        Threads::Threads
)

target_include_directories(auralis_mixer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../include
)

target_compile_features(auralis_mixer PUBLIC cxx_std_20)
