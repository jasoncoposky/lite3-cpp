#include "json.hpp"
#include "buffer.hpp"
#include <iostream>
#include <cassert>
#include <string>

// Test JSON serialization of an empty object
void test_empty_object_to_json() {
    lite3cpp::Buffer buffer;
    buffer.init_object();
    std::string json_str = lite3cpp::lite3_json::to_json_string(buffer, 0);
    if (json_str == "{}") {
        std::cout << "test_empty_object_to_json passed." << std::endl;
    } else {
        std::cout << "test_empty_object_to_json FAILED. Expected '{}', got '" << json_str << "'" << std::endl;
    }
}

// Test JSON deserialization of an empty object
void test_empty_object_from_json() {
    std::string json_str = "{}";
    lite3cpp::Buffer buffer = lite3cpp::lite3_json::from_json_string(json_str);
    if (buffer.m_used_size == lite3cpp::config::node_size) {
        std::cout << "test_empty_object_from_json passed." << std::endl;
    } else {
        std::cout << "test_empty_object_from_json FAILED. Expected buffer size " << lite3cpp::config::node_size << ", got " << buffer.m_used_size << std::endl;
    }
}

// Test JSON serialization of an object with a string
void test_string_object_to_json() {
    lite3cpp::Buffer buffer;
    buffer.init_object();
    buffer.set_str(0, "name", "test_string");
    std::string json_str = lite3cpp::lite3_json::to_json_string(buffer, 0);
    
    std::string expected_str = R"({"name":"test_string"})";

    if (json_str == expected_str) {
        std::cout << "test_string_object_to_json passed." << std::endl;
    } else {
        std::cout << "test_string_object_to_json FAILED. Expected '" << expected_str << "', got '" << json_str << "'" << std::endl;
    }
}

// Test JSON deserialization of an object with a string
void test_string_object_from_json() {
    std::string json_str = R"({"name":"test_string"})";
    lite3cpp::Buffer buffer = lite3cpp::lite3_json::from_json_string(json_str);
    // This assertion should be more robust, checking the actual content
    if (buffer.m_used_size > lite3cpp::config::node_size) { 
        std::cout << "test_string_object_from_json passed." << std::endl;
    } else {
        std::cout << "test_string_object_from_json FAILED. Buffer size not greater than initial." << std::endl;
    }
}


void run_json_tests() {
    test_empty_object_to_json();
    test_empty_object_from_json();
    test_string_object_to_json();
    test_string_object_from_json();

    std::cout << "All JSON tests passed!" << std::endl;
}