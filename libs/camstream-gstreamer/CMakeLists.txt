find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET gstreamer-1.0)

add_library(camstream-gstreamer STATIC
    src/gstreamer_pipeline.cpp
)
add_library(camstream::gstreamer ALIAS camstream-gstreamer)

target_include_directories(camstream-gstreamer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(camstream-gstreamer PUBLIC cxx_std_17)

set_target_properties(camstream-gstreamer PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_compile_options(camstream-gstreamer
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

target_link_libraries(camstream-gstreamer
    PUBLIC
        PkgConfig::GSTREAMER
)
