cmake_minimum_required(VERSION 3.16)

project(fridge_vision_stage1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_definitions(FRIDGE_CPP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

option(FRIDGE_USE_OPENCV "Enable OpenCV-based video decoding and JPG export" ON)
option(FRIDGE_BUILD_CLOUD_CLASSIFIER "Build the independent cloud fine-grained recognizer skeleton" ON)

if(FRIDGE_USE_OPENCV)
    find_package(OpenCV QUIET COMPONENTS core imgproc imgcodecs videoio)
    if(OpenCV_FOUND)
        message(STATUS "OpenCV found, real video decoding is enabled.")
    else()
        message(STATUS "OpenCV not found, falling back to frame-directory mode.")
    endif()
endif()

add_library(fridge_vision
    module_1_event_capture/src/video_io.cpp
    module_1_event_capture/src/roi_motion.cpp
    module_1_event_capture/src/frame_selector.cpp
    module_1_event_capture/src/runtime_config.cpp
    module_2_yolo_analysis/src/event_detector.cpp
    module_2_yolo_analysis/src/debug_report.cpp
    module_2_yolo_analysis/src/yolo_diff_analyzer.cpp
    module_2_yolo_analysis/src/yolo_runtime.cpp
)

target_include_directories(fridge_vision
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/module_1_event_capture/include
        ${CMAKE_CURRENT_SOURCE_DIR}/module_2_yolo_analysis/include
)

if(OpenCV_FOUND)
    target_compile_definitions(fridge_vision PUBLIC FRIDGE_USE_OPENCV=1)
    target_include_directories(fridge_vision PUBLIC ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(fridge_vision PUBLIC ${OpenCV_LIBS})
endif()

add_executable(fridge_vision_demo
    main.cpp
)

target_link_libraries(fridge_vision_demo PRIVATE fridge_vision)

add_library(fridge_inventory
    module_4_inventory/src/inventory_engine.cpp
)

target_include_directories(fridge_inventory
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/module_1_event_capture/include
        ${CMAKE_CURRENT_SOURCE_DIR}/module_4_inventory/include
)

add_library(fridge_local_service
    module_5_local_service/src/local_service.cpp
    module_5_local_service/src/service_config.cpp
)

target_include_directories(fridge_local_service
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/module_5_local_service/include
        ${CMAKE_CURRENT_SOURCE_DIR}/module_1_event_capture/include
        ${CMAKE_CURRENT_SOURCE_DIR}/module_4_inventory/include
)

target_link_libraries(fridge_local_service
    PUBLIC
        fridge_inventory
)

enable_testing()

add_executable(fridge_vision_tests
    tests/test_main.cpp
)

target_link_libraries(fridge_vision_tests PRIVATE fridge_vision)

add_test(NAME fridge_vision_tests COMMAND fridge_vision_tests)

if(FRIDGE_BUILD_CLOUD_CLASSIFIER)
    add_subdirectory(module_3_fine_grained)
endif()

add_executable(fridge_debug_module_1
    tests/debug_module_1_event_capture.cpp
)
target_link_libraries(fridge_debug_module_1 PRIVATE fridge_vision)
add_test(NAME fridge_debug_module_1 COMMAND fridge_debug_module_1)

add_executable(fridge_debug_module_2
    tests/debug_module_2_yolo_analysis.cpp
)
target_link_libraries(fridge_debug_module_2 PRIVATE fridge_vision)
add_test(NAME fridge_debug_module_2 COMMAND fridge_debug_module_2)

add_executable(fridge_debug_module_4
    tests/debug_module_4_inventory.cpp
)
target_link_libraries(fridge_debug_module_4 PRIVATE fridge_inventory)
add_test(NAME fridge_debug_module_4 COMMAND fridge_debug_module_4)

add_executable(fridge_debug_module_5
    tests/debug_module_5_local_service.cpp
)
target_link_libraries(fridge_debug_module_5 PRIVATE fridge_local_service)
add_test(NAME fridge_debug_module_5 COMMAND fridge_debug_module_5)

if(FRIDGE_BUILD_CLOUD_CLASSIFIER)
    add_executable(fridge_debug_module_3
        tests/debug_module_3_fine_grained.cpp
    )
    target_link_libraries(fridge_debug_module_3 PRIVATE fridge_cloud_classifier)
    add_test(NAME fridge_debug_module_3 COMMAND fridge_debug_module_3)
endif()
