add_executable(camstream-camera-hal-regression-tests
    src/main.cpp
)

target_compile_features(camstream-camera-hal-regression-tests PRIVATE cxx_std_17)

set_target_properties(camstream-camera-hal-regression-tests PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_compile_options(camstream-camera-hal-regression-tests
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

target_link_libraries(camstream-camera-hal-regression-tests
    PRIVATE
        camstream::camera-hal
)

add_library(camstream-camera-backend-no-registration MODULE
    src/backend_no_registration.cpp
)

add_library(camstream-camera-backend-invalid-descriptor MODULE
    src/backend_invalid_descriptor.cpp
)

add_library(camstream-camera-backend-duplicate-registration MODULE
    src/backend_duplicate_registration.cpp
)

foreach(test_backend
        camstream-camera-backend-no-registration
        camstream-camera-backend-invalid-descriptor
        camstream-camera-backend-duplicate-registration)
    target_compile_features(${test_backend} PRIVATE cxx_std_17)
    target_compile_options(${test_backend} PRIVATE -Wall -Wextra -Wpedantic)
endforeach()

target_link_libraries(camstream-camera-backend-invalid-descriptor
    PRIVATE
        camstream::camera-hal
)

target_link_libraries(camstream-camera-backend-duplicate-registration
    PRIVATE
        camstream::camera-hal
)

add_dependencies(camstream-camera-hal-regression-tests
    camstream-camera-backend-simulated
    camstream-camera-backend-no-registration
    camstream-camera-backend-invalid-descriptor
    camstream-camera-backend-duplicate-registration
)

add_test(
    NAME camera-hal-runtime-regressions
    COMMAND camstream-camera-hal-regression-tests
            $<TARGET_FILE:camstream-camera-backend-simulated>
            $<TARGET_FILE:camstream-camera-backend-no-registration>
            $<TARGET_FILE:camstream-camera-backend-invalid-descriptor>
            $<TARGET_FILE:camstream-camera-backend-duplicate-registration>
)
