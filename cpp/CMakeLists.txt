cmake_minimum_required(VERSION 3.16)

project(fridge_vision_stage1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_definitions(FRIDGE_CPP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

option(FRIDGE_USE_OPENCV "Enable OpenCV-based video decoding and JPG export" ON)
option(FRIDGE_USE_ONNXRUNTIME "Enable ONNX Runtime-based YOLO inference backend" ON)
option(FRIDGE_BUILD_CLOUD_CLASSIFIER "Build the independent cloud fine-grained recognizer skeleton" ON)
option(FRIDGE_USE_SQLITE "Enable SQLite-backed Module 4 inventory persistence adapter" ON)

set(FRIDGE_ONNXRUNTIME_ROOT "" CACHE PATH "Root directory of an ONNX Runtime SDK install")

if(FRIDGE_USE_OPENCV)
    find_package(OpenCV QUIET COMPONENTS core imgproc imgcodecs videoio dnn)
    if(OpenCV_FOUND)
        message(STATUS "OpenCV found, real video decoding is enabled.")
    else()
        message(STATUS "OpenCV with dnn support not found, falling back to frame-directory mode.")
    endif()
endif()

if(FRIDGE_USE_ONNXRUNTIME)
    set(_onnxruntime_hints)
    if(FRIDGE_ONNXRUNTIME_ROOT)
        list(APPEND _onnxruntime_hints "${FRIDGE_ONNXRUNTIME_ROOT}")
    endif()
    if(DEFINED ENV{ONNXRUNTIME_ROOT} AND NOT "$ENV{ONNXRUNTIME_ROOT}" STREQUAL "")
        list(APPEND _onnxruntime_hints "$ENV{ONNXRUNTIME_ROOT}")
    endif()

    find_path(ONNXRUNTIME_INCLUDE_DIR
        NAMES onnxruntime_cxx_api.h onnxruntime/core/session/onnxruntime_cxx_api.h
        HINTS ${_onnxruntime_hints}
        PATH_SUFFIXES include
    )
    find_library(ONNXRUNTIME_LIBRARY
        NAMES onnxruntime
        HINTS ${_onnxruntime_hints}
        PATH_SUFFIXES lib lib64
    )

    if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIBRARY)
        set(ONNXRUNTIME_FOUND TRUE)
        message(STATUS "ONNX Runtime found, module-2 native ONNX inference is enabled.")
    else()
        set(ONNXRUNTIME_FOUND FALSE)
        message(STATUS "ONNX Runtime not found, module-2 will not use the ONNX Runtime backend.")
    endif()
endif()

if(FRIDGE_USE_SQLITE)
    find_package(SQLite3 QUIET)
    if(SQLite3_FOUND)
        message(STATUS "SQLite3 found, module-4 SQLite persistence and software closure SQLite debug are enabled.")
    else()
        message(STATUS "SQLite3 development files not found, module-4 SQLite persistence adapter will not be built.")
    endif()
endif()

add_library(fridge_vision
    module_1_event_capture/src/video_io.cpp
    module_1_event_capture/src/roi_motion.cpp
    module_1_event_capture/src/frame_selector.cpp
    module_1_event_capture/src/stable_state_capture.cpp
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

if(ONNXRUNTIME_FOUND)
    target_compile_definitions(fridge_vision PUBLIC FRIDGE_USE_ONNXRUNTIME=1)
    target_include_directories(fridge_vision PUBLIC ${ONNXRUNTIME_INCLUDE_DIR})
    target_link_libraries(fridge_vision PUBLIC ${ONNXRUNTIME_LIBRARY})
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
    module_5_local_service/src/local_http_server.cpp
    module_5_local_service/src/service_config.cpp
    module_5_local_service/src/software_closure.cpp
)

if(SQLite3_FOUND)
    add_library(fridge_inventory_sqlite
        module_4_inventory/src/sqlite_inventory_store.cpp
    )
    target_include_directories(fridge_inventory_sqlite
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/module_1_event_capture/include
            ${CMAKE_CURRENT_SOURCE_DIR}/module_4_inventory/include
    )
    target_link_libraries(fridge_inventory_sqlite
        PUBLIC
            fridge_inventory
            SQLite::SQLite3
    )

    add_library(fridge_software_closure_sqlite
        module_5_local_service/src/software_closure_sqlite.cpp
    )
    target_include_directories(fridge_software_closure_sqlite
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/module_5_local_service/include
            ${CMAKE_CURRENT_SOURCE_DIR}/module_1_event_capture/include
            ${CMAKE_CURRENT_SOURCE_DIR}/module_4_inventory/include
    )
    target_compile_definitions(fridge_software_closure_sqlite PUBLIC FRIDGE_HAS_SQLITE=1)
    target_link_libraries(fridge_software_closure_sqlite
        PUBLIC
            fridge_local_service
            fridge_inventory_sqlite
    )
endif()

target_include_directories(fridge_local_service
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/module_5_local_service/include
        ${CMAKE_CURRENT_SOURCE_DIR}/module_1_event_capture/include
        ${CMAKE_CURRENT_SOURCE_DIR}/module_4_inventory/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/module_3_fine_grained/third_party
)

target_link_libraries(fridge_local_service
    PUBLIC
        fridge_inventory
)
if(WIN32)
    target_link_libraries(fridge_local_service PUBLIC ws2_32)
endif()

add_executable(fridge_local_service_server
    module_5_local_service/src/local_service_server_main.cpp
)
target_link_libraries(fridge_local_service_server PRIVATE fridge_local_service)
if(SQLite3_FOUND)
    target_link_libraries(fridge_local_service_server PRIVATE fridge_inventory_sqlite)
    target_compile_definitions(fridge_local_service_server PRIVATE FRIDGE_HAS_SQLITE=1)
endif()

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

add_executable(fridge_module2_session_runner
    module_2_yolo_analysis/demo/session_runner.cpp
)
target_link_libraries(fridge_module2_session_runner PRIVATE fridge_vision fridge_local_service)
if(SQLite3_FOUND)
    target_link_libraries(fridge_module2_session_runner PRIVATE fridge_software_closure_sqlite)
    target_compile_definitions(fridge_module2_session_runner PRIVATE FRIDGE_HAS_SQLITE=1)
endif()
target_include_directories(fridge_module2_session_runner PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/module_3_fine_grained/third_party
)

add_executable(fridge_debug_module_4
    tests/debug_module_4_inventory.cpp
)
target_link_libraries(fridge_debug_module_4 PRIVATE fridge_inventory)
add_test(NAME fridge_debug_module_4 COMMAND fridge_debug_module_4)

if(SQLite3_FOUND)
    add_executable(fridge_debug_sqlite_persistence
        tests/debug_sqlite_persistence.cpp
    )
    target_link_libraries(fridge_debug_sqlite_persistence PRIVATE fridge_inventory_sqlite)
    add_test(NAME fridge_debug_sqlite_persistence COMMAND fridge_debug_sqlite_persistence)

    add_executable(fridge_debug_software_closure_sqlite
        tests/debug_software_closure_sqlite.cpp
    )
    target_link_libraries(fridge_debug_software_closure_sqlite PRIVATE fridge_software_closure_sqlite)
    target_compile_definitions(fridge_debug_software_closure_sqlite PRIVATE FRIDGE_HAS_SQLITE=1)
    add_test(NAME fridge_debug_software_closure_sqlite COMMAND fridge_debug_software_closure_sqlite)
endif()

add_executable(fridge_debug_module_5
    tests/debug_module_5_local_service.cpp
)
target_link_libraries(fridge_debug_module_5 PRIVATE fridge_local_service)
add_test(NAME fridge_debug_module_5 COMMAND fridge_debug_module_5)

add_executable(fridge_debug_local_http_server
    tests/debug_local_http_server.cpp
)
target_link_libraries(fridge_debug_local_http_server PRIVATE fridge_local_service)
if(SQLite3_FOUND)
    target_link_libraries(fridge_debug_local_http_server PRIVATE fridge_inventory_sqlite)
    target_compile_definitions(fridge_debug_local_http_server PRIVATE FRIDGE_HAS_SQLITE=1)
endif()
add_test(NAME fridge_debug_local_http_server COMMAND fridge_debug_local_http_server)

add_executable(fridge_debug_software_closure
    tests/debug_software_closure.cpp
)
target_link_libraries(fridge_debug_software_closure PRIVATE fridge_vision fridge_local_service)
add_test(NAME fridge_debug_software_closure COMMAND fridge_debug_software_closure)

add_executable(fridge_module12_realtime_live
    tests/module12_realtime_live/main.cpp
    tests/module12_realtime_live/realtime_harness.cpp
)
target_link_libraries(fridge_module12_realtime_live PRIVATE fridge_vision fridge_local_service)
if(SQLite3_FOUND)
    target_link_libraries(fridge_module12_realtime_live PRIVATE fridge_software_closure_sqlite)
    target_compile_definitions(fridge_module12_realtime_live PRIVATE FRIDGE_HAS_SQLITE=1)
endif()
if(WIN32)
    target_link_libraries(fridge_module12_realtime_live PRIVATE ws2_32)
endif()

if(FRIDGE_BUILD_CLOUD_CLASSIFIER)
    add_executable(fridge_debug_module_3
        tests/debug_module_3_fine_grained.cpp
    )
    target_link_libraries(fridge_debug_module_3 PRIVATE fridge_cloud_classifier)
    add_test(NAME fridge_debug_module_3 COMMAND fridge_debug_module_3)
endif()
