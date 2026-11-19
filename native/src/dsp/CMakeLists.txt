add_library(auralis_dsp STATIC
    ProcessorFactory.cpp
    ProcessorLatency.cpp
    Processors.cpp
)

target_include_directories(auralis_dsp
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../include
)

target_compile_features(auralis_dsp PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(auralis_dsp PRIVATE /W4 /permissive-)
else()
    target_compile_options(auralis_dsp PRIVATE -Wall -Wextra -Wpedantic)
endif()
