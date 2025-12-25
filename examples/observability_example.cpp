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