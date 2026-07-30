add_executable(camstream-capture
    src/main.cpp
    src/mapped_buffer.cpp
    src/v4l2_device.cpp
)

target_include_directories(camstream-capture
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(camstream-capture PRIVATE cxx_std_17)

set_target_properties(camstream-capture PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_compile_options(camstream-capture
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

install(TARGETS camstream-capture
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
