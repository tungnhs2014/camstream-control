add_library(camstream-logging INTERFACE)
add_library(camstream::logging ALIAS camstream-logging)

target_include_directories(camstream-logging
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(camstream-logging INTERFACE cxx_std_17)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
