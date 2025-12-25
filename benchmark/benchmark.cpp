#include "buffer.hpp"
#include "json.hpp"
#include <iostream>
#include <chrono>
#include <vector>

void benchmark_set_str() {
    lite3cpp::Buffer buffer;
    buffer.init_object();
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10000; ++i) {
        buffer.set_str(0, "key" + std::to_string(i), "value" + std::to_string(i));
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;
    std::cout << "benchmark_set_str: " << diff.count() << " s" << std::endl;
}

void benchmark_get_str() {
    lite3cpp::Buffer buffer;
    buffer.init_object();
    for (int i = 0; i < 10000; ++i) {
        buffer.set_str(0, "key" + std::to_string(i), "value" + std::to_string(i));
    }
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10000; ++i) {
        buffer.get_str(0, "key" + std::to_string(i));
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;
    std::cout << "benchmark_get_str: " << diff.count() << " s" << std::endl;
}

void benchmark_set_i64() {
    lite3cpp::Buffer buffer;
    buffer.init_object();
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10000; ++i) {
        buffer.set_i64(0, "key" + std::to_string(i), i);
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;
    std::cout << "benchmark_set_i64: " << diff.count() << " s" << std::endl;
}

void benchmark_get_i64() {
    lite3cpp::Buffer buffer;
    buffer.init_object();
    for (int i = 0; i < 10000; ++i) {
        buffer.set_i64(0, "key" + std::to_string(i), i);
    }
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10000; ++i) {
        buffer.get_i64(0, "key" + std::to_string(i));
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;
    std::cout << "benchmark_get_i64: " << diff.count() << " s" << std::endl;
}

void benchmark_json_serialization() {
    lite3cpp::Buffer buffer;
    buffer.init_object();
    for (int i = 0; i < 10000; ++i) {
        buffer.set_i64(0, "key" + std::to_string(i), i);
    }
    auto start = std::chrono::high_resolution_clock::now();
    std::string json_str = lite3cpp::lite3_json::to_json_string(buffer, 0);
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;
    std::cout << "benchmark_json_serialization: " << diff.count() << " s" << std::endl;
}

void benchmark_json_deserialization() {
    std::string json_str = "{\"key0\":0,\"key1\":1,\"key2\":2,\"key3\":3,\"key4\":4,\"key5\":5,\"key6\":6,\"key7\":7,\"key8\":8,\"key9\":9}";
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000; ++i) {
        lite3cpp::Buffer buffer = lite3cpp::lite3_json::from_json_string(json_str);
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;
    std::cout << "benchmark_json_deserialization: " << diff.count() << " s" << std::endl;
}

int main() {
    try {
        benchmark_set_str();
    } catch (const std::exception& e) {
        std::cerr << "benchmark_set_str failed: " << e.what() << std::endl;
    }
    try {
        benchmark_get_str();
    } catch (const std::exception& e) {
        std::cerr << "benchmark_get_str failed: " << e.what() << std::endl;
    }
    /*
    try {
        benchmark_set_i64();
    } catch (const std::exception& e) {
        std::cerr << "benchmark_set_i64 failed: " << e.what() << std::endl;
    }
    try {
        benchmark_get_i64();
    } catch (const std::exception& e) {
        std::cerr << "benchmark_get_i64 failed: " << e.what() << std::endl;
    }
    try {
        benchmark_json_serialization();
    } catch (const std::exception& e) {
        std::cerr << "benchmark_json_serialization failed: " << e.what() << std::endl;
    }
    try {
        benchmark_json_deserialization();
    } catch (const std::exception& e) {
        std::cerr << "benchmark_json_deserialization failed: " << e.what() << std::endl;
    }
    */
    return 0;
}
