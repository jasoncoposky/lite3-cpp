#include "buffer.hpp"
#include <iostream>
#include <cassert>

// Forward declaration for JSON tests
void run_json_tests();

void test_buffer_creation() {
    lite3::Buffer buffer(1024);
    assert(buffer.m_data.capacity() >= 1024);
    std::cout << "test_buffer_creation passed." << std::endl;
}

void test_object_initialization() {
    lite3::Buffer buffer;
    buffer.init_object();
    assert(buffer.m_used_size == lite3::config::node_size);
    std::cout << "test_object_initialization passed." << std::endl;
}

void test_array_initialization() {
    lite3::Buffer buffer;
    buffer.init_array();
    assert(buffer.m_used_size == lite3::config::node_size);
    std::cout << "test_array_initialization passed." << std::endl;
}

int main() {
    test_buffer_creation();
    test_object_initialization();
    test_array_initialization();
    
    std::cout << "All Buffer tests passed!" << std::endl;
    run_json_tests(); // Call JSON tests
    return 0;
}
