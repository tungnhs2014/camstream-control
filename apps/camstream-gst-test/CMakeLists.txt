add_executable(camstream-gst-test
    src/main.cpp
)

target_compile_features(camstream-gst-test PRIVATE cxx_std_17)

set_target_properties(camstream-gst-test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_compile_options(camstream-gst-test
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

target_link_libraries(camstream-gst-test
    PRIVATE
        camstream::gstreamer
        camstream::logging
)

install(TARGETS camstream-gst-test
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
