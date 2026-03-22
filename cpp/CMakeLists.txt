cmake_minimum_required(VERSION 3.16)

project(fridge_vision_stage1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FRIDGE_USE_OPENCV "Enable OpenCV-based video decoding and JPG export" ON)

if(FRIDGE_USE_OPENCV)
    find_package(OpenCV QUIET COMPONENTS core imgproc imgcodecs videoio)
    if(OpenCV_FOUND)
        message(STATUS "OpenCV found, real video decoding is enabled.")
    else()
        message(STATUS "OpenCV not found, falling back to frame-directory mode.")
    endif()
endif()

add_library(fridge_vision
    src/video_io.cpp
    src/roi_motion.cpp
    src/frame_selector.cpp
    src/event_detector.cpp
    src/runtime_config.cpp
    src/debug_report.cpp
)

target_include_directories(fridge_vision
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(OpenCV_FOUND)
    target_compile_definitions(fridge_vision PUBLIC FRIDGE_USE_OPENCV=1)
    target_include_directories(fridge_vision PUBLIC ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(fridge_vision PUBLIC ${OpenCV_LIBS})
endif()

add_executable(fridge_vision_demo
    src/main.cpp
)

target_link_libraries(fridge_vision_demo PRIVATE fridge_vision)

enable_testing()

add_executable(fridge_vision_tests
    tests/test_main.cpp
)

target_link_libraries(fridge_vision_tests PRIVATE fridge_vision)

add_test(NAME fridge_vision_tests COMMAND fridge_vision_tests)
