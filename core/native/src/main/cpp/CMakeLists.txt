cmake_minimum_required(VERSION 3.22.1)

project("camera_engine")

if (ANDROID)
    add_library(${CMAKE_PROJECT_NAME} SHARED
            jni_bridge.cpp
            camera_engine.cpp)

    target_link_libraries(${CMAKE_PROJECT_NAME}
            android
            log)
else()
    option(CAMERA_ENGINE_BUILD_HOST_TESTS "Build host-side camera engine tests" ON)
    if (CAMERA_ENGINE_BUILD_HOST_TESTS)
        add_executable(camera_engine_host_tests
                camera_engine_host_tests.cpp
                camera_engine.cpp)
        target_compile_features(camera_engine_host_tests PRIVATE cxx_std_17)
        target_link_libraries(camera_engine_host_tests PRIVATE pthread)
    endif()
endif()
