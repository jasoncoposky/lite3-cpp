#include "buffer.hpp"
#include "node.hpp"
#include "utils/hash.hpp"
#include "value.hpp"
#include "exception.hpp"
#include "observability.hpp" // Add this
#include <chrono> // Add this
#include <cstring>

namespace lite3cpp {

    Buffer::Buffer() : m_used_size(0) {
        lite3cpp::log_if_enabled(lite3cpp::LogLevel::Debug, "Buffer default constructor called.", "BufferCtor", std::chrono::microseconds(0), 0);
        // Default constructor
    }

    Buffer::Buffer(size_t initial_size) : m_used_size(0) {
        lite3cpp::log_if_enabled(lite3cpp::LogLevel::Debug, "Buffer parameterized constructor called.", "BufferCtor", std::chrono::microseconds(0), 0);
        m_data.reserve(initial_size);
    }

    void Buffer::init_object() {
        lite3cpp::log_if_enabled(lite3cpp::LogLevel::Debug, "init_object called.", "InitObject", std::chrono::microseconds(0), 0);
        if (m_data.size() < config::node_size) {
            m_data.resize(config::node_size);
        }
        Node root_node;
        root_node.generation = 1;
        root_node.type = Type::Object;
        root_node.size = 0;
        root_node.key_count = 0;
        root_node.hashes.fill(0);
        root_node.kv_offsets.fill(0);
        root_node.child_offsets.fill(0);

        root_node.write(*this, 0);
        m_used_size = config::node_size;
    }

    void Buffer::init_array() {
        if (m_data.size() < config::node_size) {
            m_data.resize(config::node_size);
        }
        Node root_node;
        root_node.generation = 1;
        root_node.type = Type::Array;
        root_node.size = 0;
        root_node.key_count = 0;
        root_node.hashes.fill(0);
        root_node.kv_offsets.fill(0);
        root_node.child_offsets.fill(0);

        root_node.write(*this, 0);
        m_used_size = config::node_size;
    }

    void Buffer::set_null(size_t ofs, std::string_view key) {
        uint32_t hash = utils::djb2_hash(key);
        (void)set_impl(ofs, key, hash, 0, nullptr, Type::Null);
    }

    void Buffer::set_bool(size_t ofs, std::string_view key, bool value) {
        uint32_t hash = utils::djb2_hash(key);
        (void)set_impl(ofs, key, hash, sizeof(value), &value, Type::Bool);
    }

    void Buffer::set_i64(size_t ofs, std::string_view key, int64_t value) {
        uint32_t hash = utils::djb2_hash(key);
        (void)set_impl(ofs, key, hash, sizeof(value), &value, Type::Int64);
    }

    void Buffer::set_f64(size_t ofs, std::string_view key, double value) {
        uint32_t hash = utils::djb2_hash(key);
        (void)set_impl(ofs, key, hash, sizeof(value), &value, Type::Float64);
    }

    void Buffer::set_str(size_t ofs, std::string_view key, std::string_view value) {
        lite3cpp::log_if_enabled(lite3cpp::LogLevel::Debug, "set_str called.", "SetString", std::chrono::microseconds(0), ofs, key);
        uint32_t hash = utils::djb2_hash(key);
        (void)set_impl(ofs, key, hash, value.size(), value.data(), Type::String);
    }

    void Buffer::set_bytes(size_t ofs, std::string_view key, std::span<const std::byte> value) {
        uint32_t hash = utils::djb2_hash(key);
        (void)set_impl(ofs, key, hash, value.size(), value.data(), Type::Bytes);
    }

    size_t Buffer::set_obj(size_t ofs, std::string_view key) {
        uint32_t hash = utils::djb2_hash(key);
        return set_impl(ofs, key, hash, 0, nullptr, Type::Object);
    }

    size_t Buffer::set_arr(size_t ofs, std::string_view key) {
        uint32_t hash = utils::djb2_hash(key);
        return set_impl(ofs, key, hash, 0, nullptr, Type::Array);
    }

    void Buffer::arr_append_null(size_t ofs) {
        arr_append_impl(ofs, 0, nullptr, Type::Null);
    }

    void Buffer::arr_append_bool(size_t ofs, bool value) {
        arr_append_impl(ofs, sizeof(value), &value, Type::Bool);
    }

    void Buffer::arr_append_i64(size_t ofs, int64_t value) {
        arr_append_impl(ofs, sizeof(value), &value, Type::Int64);
    }

    void Buffer::arr_append_f64(size_t ofs, double value) {
        arr_append_impl(ofs, sizeof(value), &value, Type::Float64);
    }

    void Buffer::arr_append_str(size_t ofs, std::string_view value) {
        arr_append_impl(ofs, value.size(), value.data(), Type::String);
    }

    void Buffer::arr_append_bytes(size_t ofs, std::span<const std::byte> value) {
        arr_append_impl(ofs, value.size(), value.data(), Type::Bytes);
    }

    size_t Buffer::arr_append_obj(size_t ofs) {
        Node current_node;
        current_node.read(*this, ofs);
        if (current_node.type != Type::Array) {
            throw lite3cpp::exception("Error in buffer operation");
            return 0;
        }
        uint32_t index = current_node.size;
        return set_impl(ofs, {}, index, 0, nullptr, Type::Object);
    }

    size_t Buffer::arr_append_arr(size_t ofs) {
        Node current_node;
        current_node.read(*this, ofs);
        if (current_node.type != Type::Array) {
            throw lite3cpp::exception("Error in buffer operation");
            return 0;
        }
        uint32_t index = current_node.size;
        return set_impl(ofs, {}, index, 0, nullptr, Type::Array);
    }

    void Buffer::arr_append_impl(size_t ofs, size_t val_len, const void* val_ptr, Type type) {
        Node current_node;
        current_node.read(*this, ofs);
        if (current_node.type != Type::Array) {
            throw lite3cpp::exception("Error in buffer operation");
            return;
        }
        uint32_t index = current_node.size;
        (void)set_impl(ofs, {}, index, val_len, val_ptr, type);
    }

    bool Buffer::get_bool(size_t ofs, std::string_view key) const {
        uint32_t hash = utils::djb2_hash(key);
        Type type;
        const std::byte* value_ptr = get_impl(ofs, key, hash, type);
        if (value_ptr && type == Type::Bool) {
            return *reinterpret_cast<const bool*>(value_ptr);
        }
        throw lite3cpp::exception("Error in buffer operation");
        return false;
    }

    int64_t Buffer::get_i64(size_t ofs, std::string_view key) const {
        uint32_t hash = utils::djb2_hash(key);
        Type type;
        const std::byte* value_ptr = get_impl(ofs, key, hash, type);
        if (value_ptr && type == Type::Int64) {
            int64_t value;
            memcpy(&value, value_ptr, sizeof(value));
            return value;
        }
        throw lite3cpp::exception("Error in buffer operation");
        return 0;
    }

    double Buffer::get_f64(size_t ofs, std::string_view key) const {
        uint32_t hash = utils::djb2_hash(key);
        Type type;
        const std::byte* value_ptr = get_impl(ofs, key, hash, type);
        if (value_ptr && type == Type::Float64) {
            double value;
            memcpy(&value, value_ptr, sizeof(value));
            return value;
        }
        throw lite3cpp::exception("Error in buffer operation");
        return 0.0;
    }

    std::string_view Buffer::get_str(size_t ofs, std::string_view key) const {
        lite3cpp::log_if_enabled(lite3cpp::LogLevel::Debug, "get_str called.", "GetString", std::chrono::microseconds(0), ofs, key);
        uint32_t hash = utils::djb2_hash(key);
        Type type;
        const std::byte* value_ptr = get_impl(ofs, key, hash, type);
        if (value_ptr && type == Type::String) {
            uint32_t size;
            memcpy(&size, value_ptr, sizeof(size));
            return std::string_view(reinterpret_cast<const char*>(value_ptr + sizeof(size)), size);
        }
        throw lite3cpp::exception("Error in buffer operation");
        return {};
    }

    std::span<const std::byte> Buffer::get_bytes(size_t ofs, std::string_view key) const {
        uint32_t hash = utils::djb2_hash(key);
        Type type;
        const std::byte* value_ptr = get_impl(ofs, key, hash, type);
        if (value_ptr && type == Type::Bytes) {
            uint32_t size;
            memcpy(&size, value_ptr, sizeof(size));
            return std::span<const std::byte>(value_ptr + sizeof(size), size);
        }
        throw lite3cpp::exception("Error in buffer operation");
        return {};
    }

    size_t Buffer::get_obj(size_t ofs, std::string_view key) const {
        uint32_t hash = utils::djb2_hash(key);
        Type type;
        const std::byte* value_ptr = get_impl(ofs, key, hash, type);
        if (value_ptr && type == Type::Object) {
            return value_ptr - m_data.data() - 1;
        }
        throw lite3cpp::exception("Error in buffer operation");
        return 0;
    }

    size_t Buffer::get_arr(size_t ofs, std::string_view key) const {
        uint32_t hash = utils::djb2_hash(key);
        Type type;
        const std::byte* value_ptr = get_impl(ofs, key, hash, type);
        if (value_ptr && type == Type::Array) {
            return value_ptr - m_data.data() - 1;
        }
        throw lite3cpp::exception("Error in buffer operation");
        return 0;
    }

    bool Buffer::arr_get_bool(size_t ofs, uint32_t index) const {
        Type type;
        const std::byte* value_ptr = arr_get_impl(ofs, index, type);
        if (value_ptr && type == Type::Bool) {
            return *reinterpret_cast<const bool*>(value_ptr);
        }
        throw lite3cpp::exception("Error in buffer operation");
        return false;
    }

    int64_t Buffer::arr_get_i64(size_t ofs, uint32_t index) const {
        Type type;
        const std::byte* value_ptr = arr_get_impl(ofs, index, type);
        if (value_ptr && type == Type::Int64) {
            int64_t value;
            memcpy(&value, value_ptr, sizeof(value));
            return value;
        }
        throw lite3cpp::exception("Error in buffer operation");
        return 0;
    }

    double Buffer::arr_get_f64(size_t ofs, uint32_t index) const {
        Type type;
        const std::byte* value_ptr = arr_get_impl(ofs, index, type);
        if (value_ptr && type == Type::Float64) {
            double value;
            memcpy(&value, value_ptr, sizeof(value));
            return value;
        }
        throw lite3cpp::exception("Error in buffer operation");
        return 0.0;
    }

    std::string_view Buffer::arr_get_str(size_t ofs, uint32_t index) const {
        Type type;
        const std::byte* value_ptr = arr_get_impl(ofs, index, type);
        if (value_ptr && type == Type::String) {
            uint32_t size;
            memcpy(&size, value_ptr, sizeof(size));
            return std::string_view(reinterpret_cast<const char*>(value_ptr + sizeof(size)), size);
        }
        throw lite3cpp::exception("Error in buffer operation");
        return {};
    }

    std::span<const std::byte> Buffer::arr_get_bytes(size_t ofs, uint32_t index) const {
        Type type;
        const std::byte* value_ptr = arr_get_impl(ofs, index, type);
        if (value_ptr && type == Type::Bytes) {
            uint32_t size;
            memcpy(&size, value_ptr, sizeof(size));
            return std::span<const std::byte>(value_ptr + sizeof(size), size);
        }
        throw lite3cpp::exception("Error in buffer operation");
        return {};
    }

    size_t Buffer::arr_get_obj(size_t ofs, uint32_t index) const {
        Type type;
        const std::byte* value_ptr = arr_get_impl(ofs, index, type);
        if (value_ptr && type == Type::Object) {
            return value_ptr - m_data.data() - 1;
        }
        throw lite3cpp::exception("Error in buffer operation");
        return 0;
    }

    size_t Buffer::arr_get_arr(size_t ofs, uint32_t index) const {
        Type type;
        const std::byte* value_ptr = arr_get_impl(ofs, index, type);
        if (value_ptr && type == Type::Array) {
            return value_ptr - m_data.data() - 1;
        }
        throw lite3cpp::exception("Error in buffer operation");
        return 0;
    }

    Type Buffer::arr_get_type(size_t ofs, uint32_t index) const {
        Type type;
        arr_get_impl(ofs, index, type);
        return type;
    }

    const std::byte* Buffer::arr_get_impl(size_t ofs, uint32_t index, Type& type) const {
        Node current_node;
        current_node.read(*this, ofs);
        if (current_node.type != Type::Array) {
            throw lite3cpp::exception("Error in buffer operation");
            return nullptr;
        }
        if (index >= current_node.size) {
            throw lite3cpp::exception("Error in buffer operation");
            return nullptr;
        }

        // For arrays, the key is the index.
        // We need to construct a temporary key and hash to use get_impl
        std::string s_index = std::to_string(index);
        return get_impl(ofs, s_index, utils::djb2_hash(s_index), type);
    }

    Iterator Buffer::begin(size_t ofs) const {
        return Iterator(this, ofs, ofs);
    }

    Iterator Buffer::end(size_t ofs) const {
        return Iterator(nullptr, 0, 0);
    }

    const std::byte* Buffer::get_impl(size_t ofs, std::string_view key, uint32_t hash, Type& type) const {
        Node current_node;
        current_node.read(*this, ofs);

        size_t current_ofs = ofs;
        int node_walks = 0;
        while (true) {
            current_node.read(*this, current_ofs);
            int i = 0;
            while (i < current_node.key_count && current_node.hashes[i] < hash) {
                i++;
            }

            if (i < current_node.key_count && current_node.hashes[i] == hash) {
                // Hash collision, need to check for key equality
                size_t kv_offset = current_node.kv_offsets[i];
                
                uint32_t key_size_with_null;
                uint64_t key_data_for_storage;
                
                // Read key_data_for_storage (full uint64_t)
                memcpy(&key_data_for_storage, m_data.data() + kv_offset, sizeof(uint64_t));
                key_size_with_null = (uint32_t)(key_data_for_storage >> 2); // Extract actual key size

                std::string_view existing_key(reinterpret_cast<const char*>(m_data.data() + kv_offset + sizeof(uint64_t)), key_size_with_null - 1); // Use sizeof(uint64_t) here

                if (existing_key == key) {
                    // Keys match, return value pointer
                    size_t val_offset = kv_offset + sizeof(uint64_t) + key_size_with_null; // Use sizeof(uint64_t) here
                    type = static_cast<Type>(m_data[val_offset]);
                    return m_data.data() + val_offset + 1;
                }
                // Hash collision but keys don't match, continue search
                i++;
            }

            if (current_node.child_offsets[0] != 0) { // Not a leaf
                current_ofs = current_node.child_offsets[i];
                if (++node_walks > config::tree_height_max) {
                    throw lite3cpp::exception("Max tree height exceeded during get operation.");
                }
            } else { // Leaf node, key not found
                return nullptr;
            }
        }
    }


    size_t Buffer::set_impl(size_t ofs, std::string_view key, uint32_t hash, size_t val_len, const void* val_ptr, Type type) {
        Node current_node;
        current_node.read(*this, ofs);
    
        current_node.generation++;
        current_node.write(*this, ofs);
    
        size_t current_ofs = ofs;
        Node parent_node;
        size_t parent_ofs = 0;
        bool parent_node_loaded = false;
    
        int node_walks = 0;
        while (true) {
            current_node.read(*this, current_ofs);
            if (current_node.key_count == config::node_key_count) {
                if (parent_node_loaded) {
                    int child_index = 0;
                    while(child_index < parent_node.key_count && parent_node.child_offsets[child_index] != current_ofs) {
                        child_index++;
                    }
                    split_child(parent_node, child_index, current_node, current_ofs);
                    parent_node.write(*this, parent_ofs); // Write parent back to buffer
                    // After split, need to decide which of the two nodes to traverse
                    if (hash > parent_node.hashes[child_index]) {
                        current_ofs = parent_node.child_offsets[child_index + 1];
                    }
                } else { // split root
                    Node old_root = current_node;
                    size_t old_root_offset = m_used_size;
                    m_used_size += config::node_size;
                    if (m_data.size() < m_used_size) {
                        m_data.resize(m_used_size);
                    }
                    old_root.write(*this, old_root_offset);

                    Node new_root;
                    new_root.type = old_root.type;
                    new_root.generation = old_root.generation;
                    new_root.key_count = 0;
                    new_root.child_offsets[0] = old_root_offset;
                    new_root.size = old_root.size;

                    parent_node = new_root;
                    parent_ofs = ofs;
                    parent_node_loaded = true;
                    
                    split_child(parent_node, 0, old_root, old_root_offset);
                    parent_node.write(*this, parent_ofs);

                    current_ofs = parent_node.child_offsets[0];
                }

            }
    
            int i = 0;
            while (i < current_node.key_count && current_node.hashes[i] < hash) {
                i++;
            }
    
            if (i < current_node.key_count && current_node.hashes[i] == hash) {
                // Hash collision, need to check for key equality
                size_t kv_offset = current_node.kv_offsets[i];
                
                uint32_t key_size_with_null;
                uint64_t key_data_for_storage;
                
                // Read key_data_for_storage (full uint64_t)
                memcpy(&key_data_for_storage, m_data.data() + kv_offset, sizeof(uint64_t));
                key_size_with_null = (uint32_t)(key_data_for_storage >> 2); // Extract actual key size
            
                std::string_view existing_key(reinterpret_cast<const char*>(m_data.data() + kv_offset + sizeof(uint64_t)), key_size_with_null -1);
            
                if (existing_key == key) {
                    // Keys match, handle update
                    size_t old_val_offset = kv_offset + sizeof(uint64_t) + key_size_with_null;
                    size_t old_val_size = Value::read_size(*this, old_val_offset);
                    if (val_len <= old_val_size) {
                        // Overwrite in-place
                        Value::write(*this, old_val_offset, type, val_ptr, val_len);
                    } else {
                        // Append new value
                        // The next lines overwrite the value that was already stored
                        // So the `std::memset` should be removed and handled properly
                        // std::memset(m_data.data() + kv_offset, 0, old_val_size + key_tag_size + key_size);
            
                        size_t new_kv_offset = append_kv(key, type, val_ptr, val_len);
                        current_node.kv_offsets[i] = new_kv_offset;
                        current_node.write(*this, current_ofs);
                    }
                    if (type == Type::Object || type == Type::Array) {
                        return kv_offset + sizeof(uint64_t) + key.size() + 1; // +1 for the value type byte
                    }
                    return 0;
                }
                // Hash collision but keys don't match, continue search
                i++;
            }    
            if (current_node.child_offsets[0] != 0) { // Not a leaf
                parent_node = current_node;
                parent_ofs = current_ofs;
                parent_node_loaded = true;
                current_ofs = current_node.child_offsets[i];
                if (++node_walks > config::tree_height_max) {
                    throw lite3cpp::exception("Max tree height exceeded during set operation.");
                }
            } else { // Leaf node
                size_t kv_offset = append_kv(key, type, val_ptr, val_len);

                // Update the node
                for (int j = current_node.key_count; j > i; j--) {
                    current_node.hashes[j] = current_node.hashes[j - 1];
                    current_node.kv_offsets[j] = current_node.kv_offsets[j - 1];
                }
                current_node.hashes[i] = hash;
                current_node.kv_offsets[i] = kv_offset;
                current_node.key_count++;

                current_node.write(*this, current_ofs);

                // Update root node's size
                Node root_node;
                root_node.read(*this, 0);
                root_node.size++;
                root_node.write(*this, 0);
                
                if (type == Type::Object || type == Type::Array) {
                    // The returned offset should point to the newly created object/array node
                    // which is located after the key data and the value type byte.
                    return kv_offset + sizeof(uint64_t) + key.size() + 1;
                }
                return 0;
            }
        }
    }
    
    void Buffer::split_child(Node& parent, int child_index, Node& full_child, size_t full_child_offset) {
        Node new_sibling;
        new_sibling.type = full_child.type;
        new_sibling.generation = full_child.generation;
        new_sibling.key_count = config::node_key_count / 2;
        full_child.key_count = config::node_key_count / 2;
    
        size_t new_sibling_offset = m_used_size;
        m_used_size += config::node_size;
        if (m_data.size() < m_used_size) {
            m_data.resize(m_used_size);
        }
    
        int median_key_index = config::node_key_count / 2;
    
        // Move keys from full_child to new_sibling
        for (int i = 0; i < new_sibling.key_count; ++i) {
            new_sibling.hashes[i] = full_child.hashes[i + median_key_index + 1];
            new_sibling.kv_offsets[i] = full_child.kv_offsets[i + median_key_index + 1];
        }
    
        // Move child pointers if not a leaf
        if (full_child.child_offsets[0] != 0) {
            for (int i = 0; i <= new_sibling.key_count; ++i) {
                new_sibling.child_offsets[i] = full_child.child_offsets[i + median_key_index + 1];
            }
        }
    
        // Update parent
        for (int i = parent.key_count; i > child_index; --i) {
            parent.child_offsets[i + 1] = parent.child_offsets[i];
            parent.hashes[i] = parent.hashes[i-1];
            parent.kv_offsets[i] = parent.kv_offsets[i-1];
        }
        parent.child_offsets[child_index + 1] = new_sibling_offset;
        parent.hashes[child_index] = full_child.hashes[median_key_index];
        parent.kv_offsets[child_index] = full_child.kv_offsets[median_key_index];
        parent.key_count++;
    
        // Write all modified nodes
        full_child.write(*this, full_child_offset);
        new_sibling.write(*this, new_sibling_offset);
    }
    
    size_t Buffer::append_kv(std::string_view key, Type type, const void* val_ptr, size_t val_len) {
        // key_size_with_null is the length of the key string including the null terminator
        size_t key_size_with_null = key.size() + 1;
        
        // The key_data field will store key_size_with_null and an unused tag (since it's not a yyjson key)
        // For simplicity, we'll store key_size_with_null in the most significant bits, leaving lower bits for future use.
        uint64_t key_data_for_storage = (uint64_t)key_size_with_null << 2; // Shift by 2 to leave room for a 2-bit tag

        size_t entry_size = sizeof(uint64_t) + key_size_with_null; // Total size for key metadata and key string

        size_t value_size = val_len;
        if(type == Type::String) {
            value_size += sizeof(uint32_t);
        } else if (type == Type::Bytes) {
            value_size += sizeof(uint32_t);
        } else if (type == Type::Object || type == Type::Array) {
            value_size = config::node_size;
        }

        entry_size += 1 + value_size; // +1 for value type
    
        if (m_used_size + entry_size > m_data.capacity()) {
            m_data.reserve(m_used_size + entry_size);
        }
        if (m_used_size + entry_size > m_data.size()) {
            m_data.resize(m_used_size + entry_size);
        }
    
        size_t kv_offset = m_used_size;
    
        // Write key_data_for_storage
        memcpy(m_data.data() + m_used_size, &key_data_for_storage, sizeof(uint64_t));
        m_used_size += sizeof(uint64_t);
    
        // Write key string
        memcpy(m_data.data() + m_used_size, key.data(), key.size());
        m_used_size += key.size();
        m_data[m_used_size++] = std::byte{'\0'}; // Null terminator
    
        // Write value
        Value::write(*this, m_used_size, type, val_ptr, val_len);
    
        return kv_offset;
    }
} // namespace lite3cpp