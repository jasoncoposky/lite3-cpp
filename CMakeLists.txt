cmake_minimum_required(VERSION 3.10)
project(lite3-cpp)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LITE3CPP_BUILD_TESTS "Build lite3-cpp tests, examples, and benchmarks" OFF)
option(LITE3CPP_DISABLE_OBSERVABILITY "Disable observability features for performance" OFF)

add_library(lite3-cpp STATIC
    src/buffer.cpp
    src/iterator.cpp
    src/json.cpp
    src/node.cpp
    src/value.cpp
    src/document.cpp
    src/object.cpp
    src/array.cpp
    src/utils/hash.cpp
    src/utils/hex.cpp
    src/observability.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../yyjson/src/yyjson.c
)

target_include_directories(lite3-cpp PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/utils>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../yyjson/src>
)

if(LITE3CPP_DISABLE_OBSERVABILITY)
    target_compile_definitions(lite3-cpp PUBLIC LITE3CPP_DISABLE_OBSERVABILITY)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR OR LITE3CPP_BUILD_TESTS)
    enable_testing()
    include(FetchContent)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
      gtest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG v1.17.0
    )
    FetchContent_MakeAvailable(gtest)

    add_executable(lite3-cpp_test
        test/test_buffer_operations.cpp
        test/test_json.cpp
        test/test_observability.cpp
        test/test_modern_api.cpp
    )
    target_link_libraries(lite3-cpp_test PRIVATE lite3-cpp gtest_main)
    add_test(NAME lite3-cpp_test COMMAND lite3-cpp_test)

    add_executable(observability_example examples/observability_example.cpp)
    target_link_libraries(observability_example PRIVATE lite3-cpp)

    add_executable(lite3-cpp_benchmark benchmark/benchmark.cpp)
    target_link_libraries(lite3-cpp_benchmark PRIVATE lite3-cpp)
endif()