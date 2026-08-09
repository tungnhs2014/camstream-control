find_package(Threads REQUIRED)

add_library(camstream-camera-hal SHARED
    src/camera_backend_runtime.cpp
    src/camera_hal.cpp
    src/camera_session.cpp
)
add_library(camstream::camera-hal ALIAS camstream-camera-hal)

target_include_directories(camstream-camera-hal
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(camstream-camera-hal PUBLIC cxx_std_17)

set_target_properties(camstream-camera-hal PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_compile_options(camstream-camera-hal
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

target_link_libraries(camstream-camera-hal
    PRIVATE
        ${CMAKE_DL_LIBS}
        Threads::Threads
)

install(TARGETS camstream-camera-hal
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
