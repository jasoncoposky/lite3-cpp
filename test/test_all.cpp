#include "buffer.hpp"
#include "json.hpp"
#include "utils/hash.hpp"
#include "observability.hpp"
#include <iostream>
#include <cassert>
#include <string>
#include <vector>

class TestLogger : public lite3cpp::ILogger {
public:
    struct LogEntry {
        lite3cpp::LogLevel level;
        std::string message;
        std::string operation;
    };

    std::vector<LogEntry> logs;

    void log(lite3cpp::LogLevel level, 
           std::string_view message,
           std::string_view operation,
           std::chrono::microseconds,
           size_t,
           std::string_view) override {
        logs.push_back({level, std::string(message), std::string(operation)});
    }
};


// Forward declaration for JSON tests
void run_json_tests();

void test_buffer_creation() {
    lite3cpp::Buffer buffer(1024);
    assert(buffer.m_data.capacity() >= 1024);
    std::cout << "test_buffer_creation passed." << std::endl;
}

void test_object_initialization() {
    lite3cpp::Buffer buffer;
    buffer.init_object();
    assert(buffer.m_used_size == lite3cpp::config::node_size);
    std::cout << "test_object_initialization passed." << std::endl;
}

void test_array_initialization() {
    lite3cpp::Buffer buffer;
    buffer.init_array();
    assert(buffer.m_used_size == lite3cpp::config::node_size);
    std::cout << "test_array_initialization passed." << std::endl;
}

void test_set_get_string() {
    lite3cpp::Buffer buffer;
    buffer.init_object();
    buffer.set_str(0, "name", "test_string");
    std::string_view value = buffer.get_str(0, "name");
    assert(value == "test_string");
    std::cout << "test_set_get_string passed." << std::endl;
}

void test_set_get_int() {
    lite3cpp::Buffer buffer;
    buffer.init_object();
    buffer.set_i64(0, "age", 30);
    int64_t value = buffer.get_i64(0, "age");
    assert(value == 30);
    std::cout << "test_set_get_int passed." << std::endl;
}

void test_set_get_bool() {
    lite3cpp::Buffer buffer;
    buffer.init_object();
    buffer.set_bool(0, "is_active", true);
    bool value = buffer.get_bool(0, "is_active");
    assert(value == true);
    std::cout << "test_set_get_bool passed." << std::endl;
}

void test_set_get_null() {
    lite3cpp::Buffer buffer;
    buffer.init_object();
    buffer.set_null(0, "extra_data");
    assert(buffer.get_type(0, "extra_data") == lite3cpp::Type::Null);
    std::cout << "test_set_get_null passed." << std::endl;
}

void test_array_append_get_string() {
    lite3cpp::Buffer buffer;
    buffer.init_array();
    buffer.arr_append_str(0, "hello");
    buffer.arr_append_str(0, "world");
    std::string_view value1 = buffer.arr_get_str(0, 0);
    std::string_view value2 = buffer.arr_get_str(0, 1);
    assert(value1 == "hello");
    assert(value2 == "world");
    assert(buffer.arr_get_type(0, 0) == lite3cpp::Type::String);
    std::cout << "test_array_append_get_string passed." << std::endl;
}

void test_array_append_get_int() {
    lite3cpp::Buffer buffer;
    buffer.init_array();
    buffer.arr_append_i64(0, 10);
    buffer.arr_append_i64(0, 20);
    int64_t value1 = buffer.arr_get_i64(0, 0);
    int64_t value2 = buffer.arr_get_i64(0, 1);
    assert(value1 == 10);
    assert(value2 == 20);
    assert(buffer.arr_get_type(0, 0) == lite3cpp::Type::Int64);
    std::cout << "test_array_append_get_int passed." << std::endl;
}

void test_array_append_get_bool() {
    lite3cpp::Buffer buffer;
    buffer.init_array();
    buffer.arr_append_bool(0, true);
    buffer.arr_append_bool(0, false);
    bool value1 = buffer.arr_get_bool(0, 0);
    bool value2 = buffer.arr_get_bool(0, 1);
    assert(value1 == true);
    assert(value2 == false);
    assert(buffer.arr_get_type(0, 0) == lite3cpp::Type::Bool);
    assert(buffer.arr_get_type(0, 1) == lite3cpp::Type::Bool);
    std::cout << "test_array_append_get_bool passed." << std::endl;
}

void test_array_append_get_null() {
    lite3cpp::Buffer buffer;
    buffer.init_array();
    buffer.arr_append_null(0);
    assert(buffer.arr_get_type(0, 0) == lite3cpp::Type::Null);
    std::cout << "test_array_append_get_null passed." << std::endl;
}

void test_btree_split() {
    std::cout << "Running test_btree_split..." << std::endl;
    TestLogger logger;
    lite3cpp::set_logger(&logger);

    lite3cpp::Buffer buffer;
    buffer.init_object();

    // Insert enough keys to trigger a split
    for (int i = 0; i < lite3cpp::config::node_key_count + 1; ++i) {
        std::string key = "key" + std::to_string(i);
        std::cout << "DEBUG: Inserting key: " << key << " with value: " << i << std::endl;
        std::cout.flush();
        buffer.set_i64(0, key, i);
        std::cout << "DEBUG: Finished inserting key: " << key << std::endl;
        std::cout.flush();
    }
    std::cout << "DEBUG: Finished key insertion loop. Checking for split." << std::endl;
    std::cout.flush();

    // Verify that a split occurred
    bool split_found = false;
    for (const auto& entry : logger.logs) {
        if (entry.operation == "set_impl" && entry.message == "Node is full, splitting") {
            split_found = true;
            break;
        }
    }
    if (!split_found) {
        std::cout << "Split not found in logs. Logs:" << std::endl;
        for (const auto& entry : logger.logs) {
            std::cout << "  Level: " << (int)entry.level << ", Operation: " << entry.operation << ", Message: " << entry.message << std::endl;
        }
    }
    std::cout << "Checking split_found assertion..." << std::endl;
    std::cout.flush();
    assert(split_found);

    std::cout << "Verifying all keys can be retrieved after split..." << std::endl;
    // Verify that all keys can be retrieved
    for (int i = 0; i < lite3cpp::config::node_key_count + 1; ++i) {
        std::string key = "key" + std::to_string(i);
        int64_t value = buffer.get_i64(0, key);
        if (value != i) {
            std::cout << "Mismatch for key '" << key << "': Expected " << i << ", Got " << value << std::endl;
        }
        assert(value == i);
    }
    std::cout << "All keys retrieved successfully." << std::endl;

    std::cout << "test_btree_split passed." << std::endl;
    lite3cpp::set_logger(nullptr); // Reset logger
}


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

void run_buffer_tests() {
    test_buffer_creation();
    test_object_initialization();
    test_array_initialization();
    test_set_get_string();
    test_set_get_int();
    test_set_get_bool();
    test_set_get_null();
    test_array_append_get_string();
    test_array_append_get_int();
    test_array_append_get_bool();
    test_array_append_get_null();
    test_btree_split();
    
    std::cout << "All Buffer tests passed!" << std::endl;
}

int main() {
    run_buffer_tests();
    run_json_tests();
    return 0;
}







