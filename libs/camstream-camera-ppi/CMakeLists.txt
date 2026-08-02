add_library(camstream-camera-ppi STATIC
    src/camera_backend_module.cpp
    src/camera_session.cpp
)
add_library(camstream::camera-ppi ALIAS camstream-camera-ppi)

target_include_directories(camstream-camera-ppi
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(camstream-camera-ppi PUBLIC cxx_std_17)

set_target_properties(camstream-camera-ppi PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_compile_options(camstream-camera-ppi
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

target_link_libraries(camstream-camera-ppi
    PRIVATE
        ${CMAKE_DL_LIBS}
)

install(TARGETS camstream-camera-ppi
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
