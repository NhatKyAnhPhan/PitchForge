add_library(auralis_core INTERFACE)

get_filename_component(
    AURALIS_REPOSITORY_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/.."
    ABSOLUTE
)

target_include_directories(auralis_core
    INTERFACE
        ${AURALIS_REPOSITORY_ROOT}/include
)

target_compile_features(auralis_core INTERFACE cxx_std_20)

# Keep module integration order explicit: mixer consumes DSP factories, and the
# engine consumes both mixer and DSP. Module CMake files are supplied on their
# feature branches and remain independently buildable.
foreach(module IN ITEMS dsp mixer audio)
    set(module_cmake "${CMAKE_CURRENT_SOURCE_DIR}/src/${module}/CMakeLists.txt")
    if(EXISTS "${module_cmake}")
        add_subdirectory("src/${module}")
    endif()
endforeach()
