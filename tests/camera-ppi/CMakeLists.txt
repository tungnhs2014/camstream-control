add_executable(camstream-camera-ppi-regression-tests
    src/main.cpp
)

target_compile_features(camstream-camera-ppi-regression-tests PRIVATE cxx_std_17)

set_target_properties(camstream-camera-ppi-regression-tests PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_compile_options(camstream-camera-ppi-regression-tests
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

target_link_libraries(camstream-camera-ppi-regression-tests
    PRIVATE
        camstream::camera-ppi
        ${CMAKE_DL_LIBS}
)

add_dependencies(camstream-camera-ppi-regression-tests
    camstream-camera-backend-simulated
)

add_test(
    NAME camera-ppi-frame-ownership
    COMMAND camstream-camera-ppi-regression-tests $<TARGET_FILE:camstream-camera-backend-simulated>
)
