#include <gtest/gtest.h>
#include "observability.hpp"
#include "buffer.hpp" // Added for buffer operations
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

TEST_F(ObservabilityTest, LoggingMetricsInvocation) {
    MockLogger mock_logger;
    MockMetrics mock_metrics;

    lite3cpp::set_logger(&mock_logger);
    lite3cpp::set_metrics(&mock_metrics);
    lite3cpp::set_log_level_threshold(lite3cpp::LogLevel::Debug); // Ensure debug logs are captured

    lite3cpp::Buffer buffer;
    buffer.init_object();
    buffer.set_str(0, "test_key", "test_value");

    // Expect at least one log call for init_object and set_str each, plus any internal debug logs.
    // Exact count might vary based on internal logging. We'll assert at least one for each.
    ASSERT_GT(mock_logger.log_call_count.load(), 0);
    ASSERT_GT(mock_metrics.metric_call_count.load(), 0);

    // Perform a get operation
    buffer.get_str(0, "test_key");

    // Expect more calls after get operation
    ASSERT_GT(mock_logger.log_call_count.load(), 2); // Initial logs + get log
    ASSERT_GT(mock_metrics.metric_call_count.load(), 2); // Initial metrics + get metrics
}

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