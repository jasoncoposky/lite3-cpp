cmake_minimum_required(VERSION 3.10)
project(lite3++)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(lite3++
    src/buffer.cpp
    src/iterator.cpp
    src/json.cpp
    src/node.cpp
    src/value.cpp
    src/utils/hash.cpp
    C:/Users/jason/playground/lib/yyjson/src/yyjson.c
)

target_include_directories(lite3++ PUBLIC 
    "include"
    "include/utils"
    "C:/Users/jason/playground/lib/yyjson/src"
)

add_executable(lite3++_test
    test/test_buffer.cpp
    test/test_json.cpp
)
target_link_libraries(lite3++_test PRIVATE lite3++)

# For MSVC, ensure dynamic debug runtime library for Debug configuration
if (MSVC)
    set_target_properties(lite3++ PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreadedDebugDLL"
    )
    set_target_properties(lite3++_test PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreadedDebugDLL"
    )
endif()