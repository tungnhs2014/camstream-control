add_library(camstream-camera-backend-v4l2 MODULE
    src/camera_backend_v4l2.cpp
)

target_compile_features(camstream-camera-backend-v4l2 PRIVATE cxx_std_17)

set_target_properties(camstream-camera-backend-v4l2 PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_compile_options(camstream-camera-backend-v4l2
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

target_link_libraries(camstream-camera-backend-v4l2
    PRIVATE
        camstream::camera-hal
        camstream::logging
)

install(TARGETS camstream-camera-backend-v4l2
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/camstream/backends
)
