# lite3-cpp (formerly lite3++)
![lite3-cpp logo](assets/lite3_cpp_logo.jpg)
A C++ port of the [lite3.io](https://lite3.io/) zero copy serialization library.

## Features
*   **Modern C++ API**: Intuitive `Document`, `Value`, `Object`, `Array` proxies (`doc["key"] = 42`).
*   **Zero-Copy**: Operates directly on mutation-friendly B-Tree buffers, with a 16-byte trivially-copyable/destructible `BufferView` for zero-overhead read-only hot paths.
*   **Zero-Parse**: Read/Modify/Write without deserializing the entire document.

## Configuration & Performance

High-performance applications can disable the observability subsystem at compile time to eliminate all overhead (zero-cost).

### CMake Options

| Option | Default | Description |
| :--- | :--- | :--- |
| `LITE3CPP_DISABLE_OBSERVABILITY` | `ON` | If `ON`, all logging/metrics calls are no-ops. Set to `OFF` to enable. |
| `LITE3CPP_BUILD_COMPARE_BENCHMARK` | `OFF` | Build the optional 6-library head-to-head comparison benchmark (`benchmark/compare_benchmark.cpp`). |

**To enable observability:**
```bash
cmake -DLITE3CPP_DISABLE_OBSERVABILITY=OFF ..
```

### Measured Performance (`Release -O3`, `LITE3CPP_DISABLE_OBSERVABILITY=ON`)
7-trial medians from `benchmark/compare_benchmark.cpp` comparing `lite3-cpp` against upstream C `lite3` (`v1.1.0`), `FlexBuffers` (`v25.2.10`), ArangoDB `VelocyPack`, `simdjson` (`v4.6.9`), and `yyjson` (lower latency is better):

| Workload | `lite3-cpp` *(prehash/`_hk`)* | `lite3 (C)` *(prehash)* | `FlexBuffers` | `VelocyPack` | `simdjson` | `yyjson` |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: |
| **Cold wrap + 1 lookup (64 keys)** | 28.83 ns *(**19.39 ns**)* | 38.28 ns *(29.13 ns)* | 46.27 ns | 95.84 ns | 729.30 ns *(OD)* | 1,457.60 ns |
| **Hot i64 lookup (64 keys)** | 33.90 ns *(**23.80 ns**)* | 38.67 ns *(30.95 ns)* | 39.51 ns | 103.42 ns | 159.29 ns *(DOM)* | 97.97 ns |
| **Hot i64 lookup (1,000 keys)** | 87.18 ns *(65.89 ns)* | 80.24 ns *(**45.35 ns**)* | 133.53 ns | 274.17 ns | 2,395.92 ns *(DOM)* | 2,081.75 ns |
| **Hot string lookup (64 keys)** | 39.15 ns *(**28.56 ns**)* | 42.22 ns *(31.81 ns)* | 48.20 ns | 105.73 ns | 168.87 ns *(DOM)* | 131.01 ns |
| **Miss lookup (64 keys)** | 37.81 ns *(**17.97 ns**)* | 41.26 ns *(24.39 ns)* | 38.20 ns | 157.43 ns | — | 139.42 ns |
| **Read-Modify-Write (64 keys -> wire)** | 40.76 ns *(**33.40 ns**)* | 44.13 ns *(35.15 ns)* | 16,213.59 ns | 5,633.63 ns | — | 3,098.54 ns |
| **Build 64-key obj -> wire** | 3,789.55 ns | 3,452.61 ns | 15,265.52 ns | 6,464.07 ns | — | **2,077.68 ns** |
| **JSON Encode (64 keys -> text)** | 2,790.60 ns | 3,849.39 ns | 13,745.36 ns | 11,209.45 ns | — | **856.89 ns** |
| **JSON Decode (64 keys -> bin/DOM)** | 6,001.86 ns | 5,302.65 ns | — | 7,771.55 ns | **1,204.57 ns** | 1,305.35 ns |
| **Array build (1,000 int64s)** | 30,994.84 ns | 36,150.67 ns | 13,286.79 ns | 21,512.02 ns | — | **10,016.49 ns** |
| **Array random access (1,000 int64s)** | 58.27 ns | 55.89 ns | **2.48 ns** | 22.39 ns | — | 2.94 ns |
| **Array iteration (1,000 int64s)** | 12,517.67 ns | 13,773.00 ns | 1,499.67 ns | 7,068.37 ns | — | **1,200.64 ns** |
| **Object iteration (64 keys)** | 1,239.11 ns | 1,729.58 ns | 339.55 ns | 1,748.67 ns | — | **140.09 ns** |
| **64 KiB Blob write (`uninit+copy`)** | **2,615.93 ns** | 3,046.81 ns | 3,288.86 ns | 4,276.23 ns | — | — |
| **64 KiB Blob zero-copy read** | **21.76 ns** | 29.47 ns | 24.91 ns | 29.77 ns | — | — |
| **Wire size (64-key / 1,000-key obj)** | 3,723 B / 62,784 B | 3,723 B / 62,784 B | 1,725 B / 28,348 B | **1,589 B / 26,339 B** | — | 1,680 B / 28,507 B |

> **Notes:** Lite³ trades ~2.1–2.3x larger wire size and `O(log N)` B-tree array access for in-place mutability—yielding **138x–398x faster read-modify-write** than immutable binary formats (`VelocyPack`, `FlexBuffers`) and **25x–50x faster cold single-key extraction** than `simdjson` On-Demand and `yyjson`.

**To reproduce the comparison benchmark:**
```bash
cmake -S . -B build-compare \
  -DCMAKE_BUILD_TYPE=Release \
  -DLITE3CPP_DISABLE_OBSERVABILITY=ON \
  -DLITE3CPP_BUILD_COMPARE_BENCHMARK=ON
cmake --build build-compare --target lite3-cpp_compare_benchmark -j
./build-compare/lite3-cpp_compare_benchmark
```

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
