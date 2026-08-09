cmake_minimum_required(VERSION 3.16)

project(camstream-control LANGUAGES CXX)

include(GNUInstallDirs)

option(CAMSTREAM_BUILD_CAPTURE "Build the native V4L2 capture utility" ON)
option(CAMSTREAM_BUILD_GST_TEST "Build the GStreamer diagnostic utility" ON)
option(CAMSTREAM_BUILD_SERVICE "Build the foreground camera service" ON)
option(CAMSTREAM_BUILD_CAMERA_HAL "Build the Camera HAL core library" OFF)
option(CAMSTREAM_BUILD_CAMERA_BACKEND_SIMULATED "Build the simulated camera backend" OFF)
option(CAMSTREAM_BUILD_CAMERA_TEST "Build the Camera HAL diagnostic utility" OFF)
option(CAMSTREAM_BUILD_CAMERA_HAL_TESTS "Build host-only Camera HAL regression tests" OFF)

if(CAMSTREAM_BUILD_CAPTURE)
    add_subdirectory(apps/camstream-capture)
endif()

if(CAMSTREAM_BUILD_GST_TEST OR CAMSTREAM_BUILD_SERVICE)
    add_subdirectory(libs/camstream-gstreamer)
endif()

if(CAMSTREAM_BUILD_GST_TEST)
    add_subdirectory(apps/camstream-gst-test)
endif()

if(CAMSTREAM_BUILD_SERVICE)
    add_subdirectory(apps/camstream-service)
endif()

if(CAMSTREAM_BUILD_CAMERA_HAL OR CAMSTREAM_BUILD_CAMERA_BACKEND_SIMULATED OR CAMSTREAM_BUILD_CAMERA_TEST OR
   CAMSTREAM_BUILD_CAMERA_HAL_TESTS)
    add_subdirectory(libs/camstream-camera-hal)
endif()

if(CAMSTREAM_BUILD_CAMERA_BACKEND_SIMULATED OR CAMSTREAM_BUILD_CAMERA_HAL_TESTS)
    add_subdirectory(backends/camera/simulated)
endif()

if(CAMSTREAM_BUILD_CAMERA_TEST)
    add_subdirectory(apps/camstream-camera-test)
endif()

if(CAMSTREAM_BUILD_CAMERA_HAL_TESTS)
    enable_testing()
    add_subdirectory(tests/camera-hal)
endif()
