#include "buffer.hpp"
#include "json.hpp"
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

// Helper to pre-generate data
struct BenchmarkData {
  std::vector<std::string> keys;
  std::vector<std::string> values;

  BenchmarkData(int count) {
    keys.reserve(count);
    values.reserve(count);
    char buf[64];
    for (int i = 0; i < count; ++i) {
      snprintf(buf, sizeof(buf), "key%d", i);
      keys.emplace_back(buf);
      snprintf(buf, sizeof(buf), "value%d", i);
      values.emplace_back(buf);
    }
  }
};

void benchmark_set_str() {
  lite3cpp::Buffer buffer;
  buffer.reserve(10 * 1024 * 1024); // Reserve 10MB
  buffer.init_object();

  BenchmarkData data(10000);

  auto start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < 10000; ++i) {
    buffer.set_str(0, data.keys[i], data.values[i]);
  }
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> diff = end - start;
  std::cout << "benchmark_set_str: " << diff.count() << " s" << std::endl;
}

void benchmark_get_str() {
  lite3cpp::Buffer buffer;
  buffer.reserve(10 * 1024 * 1024); // Reserve 10MB
  buffer.init_object();

  BenchmarkData data(10000);

  for (int i = 0; i < 10000; ++i) {
    buffer.set_str(0, data.keys[i], data.values[i]);
  }

  auto start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < 10000; ++i) {
    try {
      buffer.get_str(0, data.keys[i]);
    } catch (const std::exception &e) {
      std::cerr << "benchmark_get_str failed at index " << i << ": " << e.what()
                << std::endl;
      throw;
    }
  }
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> diff = end - start;
  std::cout << "benchmark_get_str: " << diff.count() << " s" << std::endl;
}

void benchmark_set_i64() {
  lite3cpp::Buffer buffer;
  buffer.reserve(10 * 1024 * 1024); // Reserve 10MB
  buffer.init_object();

  BenchmarkData data(10000);

  auto start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < 10000; ++i) {
    buffer.set_i64(0, data.keys[i], i);
  }
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> diff = end - start;
  std::cout << "benchmark_set_i64: " << diff.count() << " s" << std::endl;
}

void benchmark_get_i64() {
  lite3cpp::Buffer buffer;
  buffer.reserve(10 * 1024 * 1024); // Reserve 10MB
  buffer.init_object();

  BenchmarkData data(10000);

  for (int i = 0; i < 10000; ++i) {
    buffer.set_i64(0, data.keys[i], i);
  }
  auto start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < 10000; ++i) {
    buffer.get_i64(0, data.keys[i]);
  }
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> diff = end - start;
  std::cout << "benchmark_get_i64: " << diff.count() << " s" << std::endl;
}

void benchmark_json_serialization() {
  lite3cpp::Buffer buffer;
  buffer.reserve(10 * 1024 * 1024); // Reserve 10MB
  buffer.init_object();
  BenchmarkData data(10000);

  for (int i = 0; i < 10000; ++i) {
    buffer.set_i64(0, data.keys[i], i);
  }
  auto start = std::chrono::high_resolution_clock::now();
  std::string json_str = lite3cpp::lite3_json::to_json_string(buffer, 0);
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> diff = end - start;
  std::cout << "benchmark_json_serialization: " << diff.count() << " s"
            << std::endl;
}

void benchmark_json_deserialization() {
  // First, create a large JSON string
  lite3cpp::Buffer buffer_to_serialize;
  buffer_to_serialize.init_object();
  BenchmarkData data(10000);

  for (int i = 0; i < 10000; ++i) {
    buffer_to_serialize.set_i64(0, data.keys[i], i);
  }
  std::string large_json_str =
      lite3cpp::lite3_json::to_json_string(buffer_to_serialize, 0);

  auto start = std::chrono::high_resolution_clock::now();
  // Only run one iteration to be comparable to serialization (if that's the
  // intent) or run multiple but account for it. The previous code had 100
  // iterations. Let's keep it but note it. Actually, let's stick to 1 iteration
  // for "large string deserialization" or keep 100. The original benchmark had
  // 100. Let's keep 100 for consistency with previous run, but the previous
  // result was 0.78s which is ~7.8ms per op.
  for (int i = 0; i < 100; ++i) {
    lite3cpp::Buffer buffer =
        lite3cpp::lite3_json::from_json_string(large_json_str);
  }
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> diff = end - start;
  std::cout << "benchmark_json_deserialization (large string, 100 iters): "
            << diff.count() << " s" << std::endl;
}

int main() {
  try {
    benchmark_set_str();
  } catch (const std::exception &e) {
    std::cerr << "benchmark_set_str failed: " << e.what() << std::endl;
  }
  try {
    benchmark_get_str();
  } catch (const std::exception &e) {
    std::cerr << "benchmark_get_str failed: " << e.what() << std::endl;
  }
  try {
    benchmark_set_i64();
  } catch (const std::exception &e) {
    std::cerr << "benchmark_set_i64 failed: " << e.what() << std::endl;
  }
  try {
    benchmark_get_i64();
  } catch (const std::exception &e) {
    std::cerr << "benchmark_get_i64 failed: " << e.what() << std::endl;
  }
  try {
    benchmark_json_serialization();
  } catch (const std::exception &e) {
    std::cerr << "benchmark_json_serialization failed: " << e.what()
              << std::endl;
  }
  try {
    benchmark_json_deserialization();
  } catch (const std::exception &e) {
    std::cerr << "benchmark_json_deserialization failed: " << e.what()
              << std::endl;
  }
  return 0;
}
