find_package(nlohmann_json CONFIG QUIET)
if(NOT TARGET nlohmann_json::nlohmann_json)
    set(FRIDGE_NLOHMANN_JSON_HEADER
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann/json.hpp
    )
    if(NOT EXISTS ${FRIDGE_NLOHMANN_JSON_HEADER})
        message(FATAL_ERROR
            "nlohmann/json was not found as a package and vendored header is missing: "
            "${FRIDGE_NLOHMANN_JSON_HEADER}"
        )
    endif()
    add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
    set_target_properties(
        nlohmann_json::nlohmann_json
        PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
    )
endif()

set(FRIDGE_CLOUD_CLASSIFIER_HAS_CURL OFF)
find_package(CURL QUIET)
if(TARGET CURL::libcurl)
    set(FRIDGE_CLOUD_CLASSIFIER_HAS_CURL ON)
else()
    find_path(FRIDGE_CURL_INCLUDE_DIR curl/curl.h)
    find_library(FRIDGE_CURL_LIBRARY NAMES curl libcurl libcurl_imp)
    if(FRIDGE_CURL_INCLUDE_DIR AND FRIDGE_CURL_LIBRARY)
        add_library(CURL::libcurl UNKNOWN IMPORTED)
        set_target_properties(
            CURL::libcurl
            PROPERTIES
                IMPORTED_LOCATION "${FRIDGE_CURL_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${FRIDGE_CURL_INCLUDE_DIR}"
        )
        set(FRIDGE_CLOUD_CLASSIFIER_HAS_CURL ON)
    endif()
endif()

add_library(fridge_cloud_classifier
    src/fine_grained_recognizer_client.cpp
)

target_include_directories(fridge_cloud_classifier
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(fridge_cloud_classifier
    PUBLIC
        nlohmann_json::nlohmann_json
)

if(FRIDGE_CLOUD_CLASSIFIER_HAS_CURL)
    target_link_libraries(fridge_cloud_classifier PUBLIC CURL::libcurl)
    target_compile_definitions(fridge_cloud_classifier PRIVATE FRIDGE_CLOUD_CLASSIFIER_HAS_CURL=1)
else()
    message(WARNING "libcurl development files were not found. Mock mode is available; remote HTTPS mode will return a runtime error.")
    target_compile_definitions(fridge_cloud_classifier PRIVATE FRIDGE_CLOUD_CLASSIFIER_HAS_CURL=0)
endif()

add_executable(fridge_cloud_classifier_demo
    demo/main.cpp
)

target_link_libraries(fridge_cloud_classifier_demo PRIVATE fridge_cloud_classifier)

add_executable(fridge_cloud_classifier_tests
    tests/test_main.cpp
)

target_link_libraries(fridge_cloud_classifier_tests PRIVATE fridge_cloud_classifier)

add_test(NAME fridge_cloud_classifier_tests COMMAND fridge_cloud_classifier_tests)
