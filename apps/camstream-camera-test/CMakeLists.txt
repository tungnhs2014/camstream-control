add_executable(camstream-camera-test
    src/main.cpp
)

target_compile_features(camstream-camera-test PRIVATE cxx_std_17)

set_target_properties(camstream-camera-test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_compile_options(camstream-camera-test
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

target_link_libraries(camstream-camera-test
    PRIVATE
        camstream::camera-hal
)

install(TARGETS camstream-camera-test
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
