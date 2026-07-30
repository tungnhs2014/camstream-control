cmake_minimum_required(VERSION 3.16)

project(camstream-control LANGUAGES CXX)

include(GNUInstallDirs)

option(CAMSTREAM_BUILD_CAPTURE "Build the native V4L2 capture utility" ON)
option(CAMSTREAM_BUILD_GST_TEST "Build the GStreamer diagnostic utility" ON)

if(CAMSTREAM_BUILD_CAPTURE)
    add_subdirectory(apps/camstream-capture)
endif()

if(CAMSTREAM_BUILD_GST_TEST)
    add_subdirectory(libs/camstream-gstreamer)
    add_subdirectory(apps/camstream-gst-test)
endif()
