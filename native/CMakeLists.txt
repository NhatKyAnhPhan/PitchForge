add_library(auralis_core INTERFACE)

target_include_directories(auralis_core
    INTERFACE
        ${PROJECT_SOURCE_DIR}/include
)

target_compile_features(auralis_core INTERFACE cxx_std_20)
