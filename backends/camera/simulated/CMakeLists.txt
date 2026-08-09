add_library(camstream-camera-backend-simulated MODULE
    src/camera_backend_simulated.cpp
)

target_compile_features(camstream-camera-backend-simulated PRIVATE cxx_std_17)

set_target_properties(camstream-camera-backend-simulated PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_compile_options(camstream-camera-backend-simulated
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

target_link_libraries(camstream-camera-backend-simulated
    PRIVATE
        camstream::camera-hal
        camstream::logging
)

install(TARGETS camstream-camera-backend-simulated
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/camstream/backends
)
