cmake_minimum_required(VERSION 3.10)
enable_testing()
project(lite3-cpp)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_Declare(
  gtest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.17.0
)
FetchContent_MakeAvailable(gtest)

add_library(lite3-cpp
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
    ${CMAKE_CURRENT_SOURCE_DIR}/../yyjson/src/yyjson.c
)
# target_compile_options(lite3-cpp PRIVATE /MTd)

target_include_directories(lite3-cpp PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/utils>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../yyjson/src>
)

add_executable(lite3-cpp_test
    test/test_buffer_operations.cpp
    test/test_json.cpp
    test/test_observability.cpp
    test/test_modern_api.cpp
)
# target_compile_options(lite3-cpp_test PRIVATE /MTd)
target_link_libraries(lite3-cpp_test PRIVATE lite3-cpp gtest_main)
add_test(NAME lite3-cpp_test COMMAND lite3-cpp_test)

add_executable(observability_example examples/observability_example.cpp)
# target_compile_options(observability_example PRIVATE /MTd)
target_link_libraries(observability_example PRIVATE lite3-cpp)

add_executable(lite3-cpp_benchmark benchmark/benchmark.cpp)
target_link_libraries(lite3-cpp_benchmark PRIVATE lite3-cpp)
option(LITE3CPP_DISABLE_OBSERVABILITY "Disable observability features for performance" OFF)

if(LITE3CPP_DISABLE_OBSERVABILITY)
    target_compile_definitions(lite3-cpp PUBLIC LITE3CPP_DISABLE_OBSERVABILITY)
endif()

# target_compile_options(lite3-cpp_benchmark PRIVATE /MTd)
target_link_libraries(lite3-cpp_benchmark PRIVATE lite3-cpp)