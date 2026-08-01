find_package(Threads REQUIRED)

add_executable(camstream-service
    src/camera_service.cpp
    src/main.cpp
)

target_include_directories(camstream-service
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(camstream-service PRIVATE cxx_std_17)

set_target_properties(camstream-service PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_compile_options(camstream-service
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

target_link_libraries(camstream-service
    PRIVATE
        Threads::Threads
)

install(TARGETS camstream-service
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
