cmake_minimum_required(VERSION 3.16)

project(cpp_infer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(cpp_infer_placeholder
    src/main.cpp
)

target_include_directories(cpp_infer_placeholder
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
