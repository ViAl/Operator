cmake_minimum_required(VERSION 3.22.1)

project("camera_engine")

add_library(${CMAKE_PROJECT_NAME} SHARED
        jni_bridge.cpp
        camera_engine.cpp)

target_link_libraries(${CMAKE_PROJECT_NAME}
        android
        log)
