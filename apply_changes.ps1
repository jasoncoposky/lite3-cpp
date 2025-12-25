# Create new files
Write-Output "Creating new files..."

@'
#include <gtest/gtest.h>
#include "observability.hpp"
#include <thread>
#include <vector>
#include <atomic>

// Mock Logger and Metrics for testing
class MockLogger : public lite3cpp::ILogger {
public:
    std::atomic<int> log_call_count{0};
    bool log(lite3cpp::LogLevel, std::string_view, std::string_view, std::chrono::microseconds, size_t, std::string_view) override {
        log_call_count++;
        return true;
    }
};

class MockMetrics : public lite3cpp::IMetrics {
public:
    std::atomic<int> metric_call_count{0};
    bool record_latency(std::string_view, double) override { metric_call_count++; return true; }
    bool increment_operation_count(std::string_view, std::string_view) override { metric_call_count++; return true; }
    bool set_buffer_usage(size_t) override { metric_call_count++; return true; }
    bool set_buffer_capacity(size_t) override { metric_call_count++; return true; }
    bool increment_node_splits() override { metric_call_count++; return true; }
    bool increment_hash_collisions() override { metric_call_count++; return true; }
};

// Test fixture to reset global state
struct ObservabilityTest : public ::testing::Test {
    void SetUp() override {
        // Ensure globals are reset to null implementations before each test
        lite3cpp::set_logger(nullptr);
        lite3cpp::set_metrics(nullptr);
    }
};

TEST_F(ObservabilityTest, ThreadSafetySetLogger) {
    MockLogger mock_logger1;
    MockLogger mock_logger2;
    const int num_threads = 100;
    std::vector<std::thread> threads;

    std::atomic<int> ready_threads = 0;
    std::atomic<bool> start_test = false;

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&, i]() {
            ready_threads++;
            while (!start_test) {
                std::this_thread::yield(); // Wait for signal to start
            }
            if (i % 2 == 0) {
                lite3cpp::set_logger(&mock_logger1);
            } else {
                lite3cpp::set_logger(&mock_logger2);
            }
        });
    }

    // Wait for all threads to be ready
    while (ready_threads < num_threads) {
        std::this_thread::yield();
    }

    // Signal all threads to start concurrently
    start_test = true;

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_NE(lite3cpp::g_logger.load(std::memory_order_acquire), nullptr);
}

TEST_F(ObservabilityTest, ThreadSafetySetMetrics) {
    MockMetrics mock_metrics1;
    MockMetrics mock_metrics2;
    const int num_threads = 100;
    std::vector<std::thread> threads;

    std::atomic<int> ready_threads = 0;
    std::atomic<bool> start_test = false;

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&, i]() {
            ready_threads++;
            while (!start_test) {
                std::this_thread::yield(); // Wait for signal to start
            }
            if (i % 2 == 0) {
                lite3cpp::set_metrics(&mock_metrics1);
            } else {
                lite3cpp::set_metrics(&mock_metrics2);
            }
        });
    }

    // Wait for all threads to be ready
    while (ready_threads < num_threads) {
        std::this_thread::yield();
    }

    // Signal all threads to start concurrently
    start_test = true;

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_NE(lite3cpp::g_metrics.load(std::memory_order_acquire), nullptr);
}
'@ | Set-Content -Path "test/test_observability.cpp" -Encoding UTF8

@' 
#ifndef LITE3_OBSERVABILITY_HPP
#define LITE3_OBSERVABILITY_HPP

#include <string_view>
#include <chrono>
#include <atomic>

namespace lite3cpp {

enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error
};

class ILogger {
public:
    virtual ~ILogger() = default;
    virtual bool log(LogLevel level, 
                   std::string_view message,
                   std::string_view operation,
                   std::chrono::microseconds duration,
                   size_t buffer_offset,
                   std::string_view key = "") = 0;
};

class IMetrics {
public:
    virtual ~IMetrics() = default;
    virtual bool record_latency(std::string_view operation, double seconds) = 0;
    virtual bool increment_operation_count(std::string_view operation, std::string_view status) = 0;
    virtual bool set_buffer_usage(size_t used_bytes) = 0;
    virtual bool set_buffer_capacity(size_t capacity_bytes) = 0;
    virtual bool increment_node_splits() = 0;
    virtual bool increment_hash_collisions() = 0;
};

// Global atomic pointers for the current logger and metrics implementations.
// The lite3-cpp library does NOT take ownership of the objects pointed to by g_logger or g_metrics.
// Their lifetime must be managed by the caller.
extern std::atomic<ILogger*> g_logger;
extern std::atomic<IMetrics*> g_metrics;

// Sets the global logger.
// The lite3-cpp library does NOT take ownership of the 'logger' object.
// The caller is responsible for ensuring the 'logger' object remains valid
// for the entire duration it is set and used by the library.
void set_logger(ILogger* logger);

// Sets the global metrics collector.
// The lite3-cpp library does NOT take ownership of the 'metrics' object.
// The caller is responsible for ensuring the 'metrics' object remains valid
// for the entire duration it is set and used by the library.
void set_metrics(IMetrics* metrics);

// Global atomic log level threshold. Messages with a level lower than this threshold
// will not be processed by the global logger. Defaults to LogLevel::Info.
extern std::atomic<LogLevel> g_log_level_threshold;

// Sets the global log level threshold.
// Log messages with a level lower than the set threshold will be filtered out.
void set_log_level_threshold(LogLevel level);

template<typename... Args>
inline bool log_if_enabled(LogLevel level, std::string_view message, std::string_view operation,
                           std::chrono::microseconds duration, size_t buffer_offset, std::string_view key = "") {
    if (level >= g_log_level_threshold.load(std::memory_order_acquire)) {
        ILogger* logger = g_logger.load(std::memory_order_acquire);
        if (logger) { // Should always be true due to NullLogger, but good practice
            return logger->log(level, message, operation, duration, buffer_offset, key);
        }
    }
    return true; // Indicate that logging was attempted. Individual logger can fail.
}

} // namespace lite3cpp

#endif // LITE3_OBSERVABILITY_HPP
'@ | Set-Content -Path "include/observability.hpp" -Encoding UTF8

@' 
#include "observability.hpp"

namespace lite3cpp {

class NullLogger : public ILogger {
public:
    bool log(LogLevel, std::string_view, std::string_view, std::chrono::microseconds, size_t, std::string_view) override { return true; }
};

class NullMetrics : public IMetrics {
public:
    bool record_latency(std::string_view, double) override { return true; }
    bool increment_operation_count(std::string_view, std::string_view) override { return true; }
    bool set_buffer_usage(size_t) override { return true; }
    bool set_buffer_capacity(size_t) override { return true; }
    bool increment_node_splits() override { return true; }
    bool increment_hash_collisions() override { return true; }
};

static NullLogger null_logger;
static NullMetrics null_metrics;

std::atomic<ILogger*> g_logger = &null_logger;
std::atomic<IMetrics*> g_metrics = &null_metrics;

void set_logger(ILogger* logger) {
    if (logger) {
        g_logger.store(logger, std::memory_order_release);
    } else {
        g_logger.store(&null_logger, std::memory_order_release);
    }
}

void set_metrics(IMetrics* metrics) {
    if (metrics) {
        g_metrics.store(metrics, std::memory_order_release);
    } else {
        g_metrics.store(&null_metrics, std::memory_order_release);
    }
}

std::atomic<LogLevel> g_log_level_threshold = LogLevel::Info;

void set_log_level_threshold(LogLevel level) {
    g_log_level_threshold.store(level, std::memory_order_release);
}

} // namespace lite3cpp
'@ | Set-Content -Path "src/observability.cpp" -Encoding UTF8

@' 
#include "observability.hpp"
#include "buffer.hpp"
#include <iostream>

class ConsoleLogger : public lite3cpp::ILogger {
public:
    bool log(lite3cpp::LogLevel level, 
           std::string_view message,
           std::string_view operation,
           std::chrono::microseconds duration,
           size_t buffer_offset,
           std::string_view key) override {
        std::cout << "[LogLevel::" << static_cast<int>(level) << "] " // Added LogLevel name
                  << message << " | "
                  << "operation: " << operation << " | "
                  << "duration: " << duration.count() << "us | "
                  << "offset: " << buffer_offset << " | "
                  << "key: " << key
                  << std::endl;
        return true;
    }
};


class ConsoleMetrics : public lite3cpp::IMetrics {
public:
    bool record_latency(std::string_view operation, double seconds) override {
        std::cout << "Metric: " << operation << " latency: " << seconds << "s" << std::endl;
        return true;
    }
    bool increment_operation_count(std::string_view operation, std::string_view status) override {
        std::cout << "Metric: " << operation << " count: 1, status: " << status << std::endl;
        return true;
    }
    bool set_buffer_usage(size_t used_bytes) override {
        std::cout << "Metric: buffer usage: " << used_bytes << " bytes" << std::endl;
        return true;
    }
    bool set_buffer_capacity(size_t capacity_bytes) override {
        std::cout << "Metric: buffer capacity: " << capacity_bytes << " bytes" << std::endl;
        return true;
    }
    bool increment_node_splits() override {
        std::cout << "Metric: node splits: 1" << std::endl;
        return true;
    }
    bool increment_hash_collisions() override {
        std::cout << "Metric: hash collisions: 1" << std::endl;
        return true;
    }
};

int main() {
    ConsoleLogger logger;
    ConsoleMetrics metrics;
    lite3cpp::set_logger(&logger);
    lite3cpp::set_metrics(&metrics);

    std::cout << "--- Initial logging attempts (should show nothing unless library calls log) ---" << std::endl;
    // These calls will be filtered out by the default LogLevel::Info threshold
    lite3cpp::log_if_enabled(lite3cpp::LogLevel::Debug, "Debug message", "example", std::chrono::microseconds(10), 0);
    lite3cpp::log_if_enabled(lite3cpp::LogLevel::Info, "Info message", "example", std::chrono::microseconds(20), 0);
    
    std::cout << "\n--- Setting log level to Warn ---" << std::endl;
    lite3cpp::set_log_level_threshold(lite3cpp::LogLevel::Warn);

    std::cout << "Attempting to log Debug (should be filtered)" << std::endl;
    lite3cpp::log_if_enabled(lite3cpp::LogLevel::Debug, "Debug message (filtered)", "example", std::chrono::microseconds(30), 0);
    std::cout << "Attempting to log Info (should be filtered)" << std::endl;
    lite3cpp::log_if_enabled(lite3cpp::LogLevel::Info, "Info message (filtered)", "example", std::chrono::microseconds(40), 0);
    std::cout << "Attempting to log Warn (should pass)" << std::endl;
    lite3cpp::log_if_enabled(lite3cpp::LogLevel::Warn, "Warning message (passed)", "example", std::chrono::microseconds(50), 0);

    std::cout << "\n--- Setting log level back to Debug ---" << std::endl;
    lite3cpp::set_log_level_threshold(lite3cpp::LogLevel::Debug);

    std::cout << "Attempting to log Debug (should pass)" << std::endl;
    lite3cpp::log_if_enabled(lite3cpp::LogLevel::Debug, "Debug message (passed)", "example", std::chrono::microseconds(60), 0);
    std::cout << "Attempting to log Info (should pass)" << std::endl;
    lite3cpp::log_if_enabled(lite3cpp::LogLevel::Info, "Info message (passed)", "example", std::chrono::microseconds(70), 0);

    return 0;
}
'@ | Set-Content -Path "examples/observability_example.cpp" -Encoding UTF8

# Overwrite CMakeLists.txt
Write-Output "Overwriting CMakeLists.txt..."
@' 
cmake_minimum_required(VERSION 3.10)
enable_testing()
project(lite3-cpp)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
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
    src/utils/hash.cpp
    src/observability.cpp
    C:/Users/jason/playground/lib/yyjson/src/yyjson.c
)
target_compile_options(lite3-cpp PRIVATE /MTd)

target_include_directories(lite3-cpp PUBLIC 
    "include"
    "include/utils"
    "C:/Users/jason/playground/lib/yyjson/src"
)

add_executable(lite3-cpp_test
    test/test_buffer.cpp
    test/test_json.cpp
    test/test_observability.cpp
)
target_compile_options(lite3-cpp_test PRIVATE /MTd)
target_link_libraries(lite3-cpp_test PRIVATE lite3-cpp gtest_main)
add_test(NAME lite3-cpp_test COMMAND lite3-cpp_test)

add_executable(observability_example examples/observability_example.cpp)
target_compile_options(observability_example PRIVATE /MTd)
target_link_libraries(observability_example PRIVATE lite3-cpp)
'@ | Set-Content -Path "CMakeLists.txt" -Encoding UTF8

# Overwrite README.md
Write-Output "Overwriting README.md..."
@' 
# lite3-cpp (formerly lite3++)
A C++ port of the lite3-cpp zero copy serialization library

## Observability Interface

The `lite3-cpp` library provides an extensible observability interface through `ILogger` and `IMetrics` to allow users to integrate their custom logging and metrics collection systems.

### `ILogger`

The `ILogger` interface allows you to plug in a custom logging backend. You can implement this interface and set your logger instance to receive detailed log messages from the library.

**Example Implementation (ConsoleLogger):**
```cpp
#include "observability.hpp"
#include <iostream>
#include <string_view>

class ConsoleLogger : public lite3cpp::ILogger {
public:
    bool log(lite3cpp::LogLevel level,
           std::string_view message,
           std::string_view operation,
           std::chrono::microseconds duration,
           size_t buffer_offset,
           std::string_view key) override {
        std::cout << "[LogLevel::" << static_cast<int>(level) << "] "
                  << message << " | "
                  << "operation: " << operation << " | "
                  << "duration: " << duration.count() << "us | "
                  << "offset: " << buffer_offset << " | "
                  << "key: " << key
                  << std::endl;
        return true;
    }
};
```

### `IMetrics`

The `IMetrics` interface provides hooks for collecting various performance and usage metrics from the library.

**Example Implementation (ConsoleMetrics):**
```cpp
#include "observability.hpp"
#include <iostream>
#include <string_view>

class ConsoleMetrics : public lite3cpp::IMetrics {
public:
    bool record_latency(std::string_view operation, double seconds) override { return true; }
    bool increment_operation_count(std::string_view operation, std::string_view status) override { return true; }
    bool set_buffer_usage(size_t used_bytes) override { return true; }
    bool set_buffer_capacity(size_t capacity_bytes) override { return true; }
    bool increment_node_splits() override { return true; }
    bool increment_hash_collisions() override { return true; }
};
```

### Metric Naming Conventions

To ensure consistency and facilitate easier analysis in external monitoring systems, it is recommended to follow a consistent naming convention for `operation` strings used in `IMetrics` methods (e.g., `record_latency`, `increment_operation_count`).

A suggested convention is `ComponentName.OperationName` or `ComponentName.Subsystem.OperationName`.

**Examples:**
*   `lite3-cpp.Buffer.SetString`
*   `lite3-cpp.Node.NodeWrite`
*   `lite3-cpp.Json.JsonParse`
*   `lite3-cpp.Iterator.IteratorNext`

### Setting Custom Implementations

You can set your custom `ILogger` and `IMetrics` implementations using the `lite3cpp::set_logger` and `lite3cpp::set_metrics` functions:

```cpp
#include "observability.hpp"
// Include your custom Logger and Metrics headers here

int main() {
    MyCustomLogger logger;
    MyCustomMetrics metrics;

    lite3cpp::set_logger(&logger);
    lite3cpp::set_metrics(&metrics);

    // ... Your lite3cpp library usage here ...

    // To reset to default null implementations (no-op), you can pass nullptr
    lite3cpp::set_logger(nullptr);
    lite3cpp::set_metrics(nullptr);

    return 0;
}
```

### Ownership Semantics

It is crucial to understand that the `lite3-cpp` library does **NOT** take ownership of the `ILogger` and `IMetrics` objects passed to `lite3cpp::set_logger` and `lite3cpp::set_metrics`. The caller is entirely responsible for managing the lifetime of these objects. Ensure that the logger and metrics instances remain valid and alive for as long as they are set and used by the `lite3-cpp` library to avoid use-after-free bugs.

### Thread Safety

The `lite3cpp::set_logger` and `lite3cpp::set_metrics` functions are thread-safe, allowing for concurrent updates to the global logger and metrics instances.

### Log Level Filtering

The `lite3-cpp` library allows you to control the verbosity of log messages through a global log level threshold. Messages with a `lite3cpp::LogLevel` lower than the set threshold will be filtered out and not passed to the `ILogger` implementation.

You can set the global log level threshold using `lite3cpp::set_log_level_threshold`:

```cpp
#include "observability.hpp"

int main() {
    // Set a logger and metrics collector first (as shown above)
    // ConsoleLogger logger;
    // lite3cpp::set_logger(&logger);

    // Set the log level threshold to Warning.
    // Only messages with lite3cpp::LogLevel::Warn or lite3cpp::LogLevel::Error will be processed.
    lite3cpp::set_log_level_threshold(lite3cpp::LogLevel::Warn);

    // ... library operations that generate logs ...

    // Reset to default (lite3cpp::LogLevel::Info)
    lite3cpp::set_log_level_threshold(lite3cpp::LogLevel::Info);

    return 0;
}
```

The internal logging calls within the `lite3-cpp` library use a helper function, `lite3cpp::log_if_enabled`, which automatically checks the global log level threshold before dispatching to your custom `ILogger` implementation. You can also use this helper function in your own code to ensure consistency with the `lite3-cpp` library's filtering logic.

### Error Handling in Implementations

The `log` method in `ILogger` and all methods in `IMetrics` now return a `bool` value. This return value indicates whether the observability operation (e.g., logging a message, recording a metric) was successfully *processed by the underlying implementation*.

*   **`true`**: The operation was successfully processed by the implementation.
*   **`false`**: The operation failed within the implementation (e.g., failed to write to a log file, failed to send metrics over a network).

The `lite3-cpp` library's core logic will generally *ignore* the return value from these methods to ensure that observability failures do not disrupt the primary application flow. However, implementers of `ILogger` and `IMetrics` can use this return value internally to handle their own errors (e.g., retries, fallback mechanisms, internal error logging).
'@ | Set-Content -Path "README.md" -Encoding UTF8

# Overwrite .gitignore
Write-Output "Overwriting .gitignore..."
@' 
# Prerequisites
*.d

# Compiled Object files
*.slo
*.lo
*.o
*.obj

# Precompiled Headers
*.gch
*.pch

# Linker files
*.ilk

# Debugger Files
*.pdb

# Compiled Dynamic libraries
*.so
*.dylib
*.dll

# Fortran module files
*.mod
*.smod

# Compiled Static libraries
*.lai
*.la
*.a
*.lib

# Executables
*.exe
*.out
*.app

# debug information files
*.dwo

# Build directories
build/
build-test/
_deps/

# Visual Studio files
.vs/
*.sln
*.suo
*.user
*.vcxproj
*.vcxproj.filters

# CMake files
CMakeCache.txt
CMakeFiles/
cmake_install.cmake
install/
CTestTestfile.cmake
_CPack_Packages/
'@ | Set-Content -Path ".gitignore" -Encoding UTF8

# Modify existing C++ files
Write-Output "Modifying existing C++ files..."

$filesToModify = @(
    "include\node.hpp",
    "include\iterator.hpp",
    "include\exception.hpp",
    "include\buffer.hpp",
    "include\json.hpp",
    "include\value.hpp",
    "include\config.hpp",
    "include\utils\hash.hpp",
    "src\buffer.cpp",
    "src\iterator.cpp",
    "src\json.cpp",
    "src\node.cpp",
    "src\value.cpp",
    "src\utils\hash.cpp",
    "test\test_json.cpp",
    "test\test_buffer.cpp",
    "test\test_all.cpp"
)

foreach ($file in $filesToModify) {
    $filePath = Join-Path $pwd $file
    $content = Get-Content $filePath -Raw

    # Correct namespace usage
    $newContent = $content.Replace("namespace lite3 {", "namespace lite3cpp {")
    $newContent = $newContent.Replace("lite3::", "lite3cpp::")
    
    # Correct closing namespace comments
    $newContent = $newContent.Replace("} // namespace lite3", "} // namespace lite3cpp")
    $newContent = $newContent.Replace("} // namespace lite3::utils", "} // namespace lite3cpp::utils")
    
    # Observability calls and error handling
    # Apply specific content fixes from previous attempts
    if ($file -eq "src\buffer.cpp") {
        # Add observability includes
        $newContent = $newContent.Replace("#include <cstring>", "#include <cstring>`n#include \"observability.hpp\"`n#include <chrono>")
        # Add log calls to constructors and functions
        $newContent = $newContent.Replace("    Buffer::Buffer() : m_used_size(0) {", "    Buffer::Buffer() : m_used_size(0) {`n        lite3cpp::log_if_enabled(lite3cpp::LogLevel::Debug, \"Buffer default constructor called.\", \"BufferCtor\", std::chrono::microseconds(0), 0);")
        $newContent = $newContent.Replace("    Buffer::Buffer(size_t initial_size) : m_used_size(0) {", "    Buffer::Buffer(size_t initial_size) : m_used_size(0) {`n        lite3cpp::log_if_enabled(lite3cpp::LogLevel::Debug, \"Buffer parameterized constructor called.\", \"BufferCtor\", std::chrono::microseconds(0), 0);")
        $newContent = $newContent.Replace("    void Buffer::init_object() {", "    void Buffer::init_object() {`n        lite3cpp::log_if_enabled(lite3cpp::LogLevel::Debug, \"init_object called.\", \"InitObject\", std::chrono::microseconds(0), 0);")
        $newContent = $newContent.Replace("    void Buffer::set_str(size_t ofs, std::string_view key, std::string_view value) {", "    void Buffer::set_str(size_t ofs, std::string_view key, std::string_view value) {`n        lite3cpp::log_if_enabled(lite3cpp::LogLevel::Debug, \"set_str called.\", \"SetString\", std::chrono::microseconds(0), ofs, key);")
        $newContent = $newContent.Replace("std::string_view Buffer::get_str(size_t ofs, std::string_view key) const {", "std::string_view Buffer::get_str(size_t ofs, std::string_view key) const {`n        lite3cpp::log_if_enabled(lite3cpp::LogLevel::Debug, \"get_str called.\", \"GetString\", std::chrono::microseconds(0), ofs, key);")
        # Implement TODO exceptions
        $newContent = $newContent.Replace("                if (++node_walks > config::tree_height_max) {
                    // TODO: Throw exception
                    return nullptr;
                }", "                if (++node_walks > config::tree_height_max) {
                    throw lite3cpp::exception(\"Max tree height exceeded during get operation.\");
                }")
        $newContent = $newContent.Replace("                if (++node_walks > config::tree_height_max) {
                    // TODO: Throw exception
                    return 0;
                }", "                if (++node_walks > config::tree_height_max) {
                    throw lite3cpp::exception(\"Max tree height exceeded during set operation.\");
                }")
        # Remove old iostream include
        $newContent = $newContent.Replace("#include <iostream> // For debugging", "")
    }
    if ($file -eq "src\iterator.cpp") {
        $newContent = $newContent.Replace("#include <cstring>  // For memcpy", "#include <cstring>  // For memcpy`n#include \"observability.hpp\"`n#include <chrono>")
        $newContent = $newContent.Replace("void Iterator::find_next() {", "void Iterator::find_next() {`n        lite3cpp::log_if_enabled(lite3cpp::LogLevel::Debug, \"Iterator::find_next called.\", \"IteratorNext\", std::chrono::microseconds(0), 0);")
    }
    if ($file -eq "src\json.cpp") {
        $newContent = $newContent.Replace("#include <vector>", "#include <vector>`n#include \"observability.hpp\"`n#include <chrono>")
        $newContent = $newContent.Replace("        std::string to_json_string(const Buffer& buffer, size_t ofs) {", "        std::string to_json_string(const Buffer& buffer, size_t ofs) {`n            lite3cpp::log_if_enabled(lite3cpp::LogLevel::Info, \"JSON stringify started.\", \"JsonStringify\", std::chrono::microseconds(0), ofs);")
        $newContent = $newContent.Replace("        Buffer from_json_string(const std::string& json_str) {", "        Buffer from_json_string(const std::string& json_str) {`n            lite3cpp::log_if_enabled(lite3cpp::LogLevel::Info, \"JSON parse started.\", \"JsonParse\", std::chrono::microseconds(0), 0);")
        $newContent = $newContent.Replace("            if (!doc) {
                // TODO: throw exception
                return Buffer();
            }", "            if (!doc) {
                throw lite3cpp::exception(\"Invalid JSON string provided.\");
            }")
    }
    if ($file -eq "src\node.cpp") {
        $newContent = $newContent.Replace("#include <cstring>", "#include <cstring>`n#include \"observability.hpp\"`n#include <chrono>")
        $newContent = $newContent.Replace("lite3cpp::log_if_enabled(lite3cpp::LogLevel::Debug, \"Node::read - Read node.\", \"NodeRead\", std::chrono::microseconds(0), offset);", "        lite3cpp::log_if_enabled(lite3cpp::LogLevel::Debug, \"Node::read - Read node.\", \"NodeRead\", std::chrono::microseconds(0), offset);") # This line is already in the file. 
        $newContent = $newContent.Replace("lite3cpp::log_if_enabled(lite3cpp::LogLevel::Debug, \"Node::write - Wrote node.\", \"NodeWrite\", std::chrono::microseconds(0), offset);", "        lite3cpp::log_if_enabled(lite3cpp::LogLevel::Debug, \"Node::write - Wrote node.\", \"NodeWrite\", std::chrono::microseconds(0), offset);") # This line is also in the file.
    }
    # Fix test/test_json.cpp and test/test_all.cpp json calls
    if ($file -eq "test\test_json.cpp" -or $file -eq "test\test_all.cpp") {
        $newContent = $newContent.Replace("lite3cpp::to_json_string(", "lite3cpp::lite3_json::to_json_string(")
        $newContent = $newContent.Replace("lite3cpp::from_json_string(", "lite3cpp::lite3_json::from_json_string(")
    }
    Set-Content $filePath $newContent -Encoding UTF8
}