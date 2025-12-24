#include "iterator.hpp"
#include "buffer.hpp"
#include <cstring>  // For memcpy

namespace lite3 {

    Iterator::Iterator(const Buffer* buffer, size_t ofs, size_t node_offset) : m_buffer(buffer), m_depth(-1) {
        if(m_buffer) {
            m_depth = 0;
            m_stack[m_depth] = {node_offset, 0};
            find_first();
            if(m_depth >= 0) {
                find_next();
            }
        }
    }

    Iterator& Iterator::operator++() {
        find_next();
        return *this;
    }

    const Iterator::value_type& Iterator::operator*() const {
        return m_current_value;
    }

    const Iterator::value_type* Iterator::operator->() const {
        return &m_current_value;
    }

    bool Iterator::operator==(const Iterator& other) const {
        if (!m_buffer && !other.m_buffer) return true;
        if (!m_buffer || !other.m_buffer) return false;
        
        return m_buffer == other.m_buffer && m_depth == other.m_depth &&
               m_stack[m_depth].offset == other.m_stack[other.m_depth].offset &&
               m_stack[m_depth].key_index == other.m_stack[other.m_depth].key_index;
    }

    bool Iterator::operator!=(const Iterator& other) const {
        return !(*this == other);
    }

    void Iterator::find_first() {
        Node current_node;
        current_node.read(*m_buffer, m_stack[m_depth].offset);

        while(current_node.child_offsets[0] != 0) {
            m_depth++;
            m_stack[m_depth] = {current_node.child_offsets[0], 0};
            current_node.read(*m_buffer, m_stack[m_depth].offset);
        }
    }

    void Iterator::find_next() {
        if(m_depth < 0) {
            m_buffer = nullptr; // Mark as end
            return;
        }
        
        Node current_node;
        current_node.read(*m_buffer, m_stack[m_depth].offset);

        if (m_stack[m_depth].key_index >= current_node.key_count) {
            m_depth--;
            if (m_depth < 0) {
                m_buffer = nullptr; // Mark as end
                return;
            }
            find_next();
            return;
        }
        
        size_t kv_offset = current_node.kv_offsets[m_stack[m_depth].key_index];

        uint32_t key_size_with_null = 0;
        uint64_t key_data_for_storage;
        
        std::memcpy(&key_data_for_storage, m_buffer->m_data.data() + kv_offset, sizeof(uint64_t));
        key_size_with_null = (uint32_t)(key_data_for_storage >> 2);

        if (key_size_with_null == 0) {
            m_buffer = nullptr; // Terminate iteration
            return;
        }

        m_current_value.key = std::string_view(reinterpret_cast<const char*>(m_buffer->m_data.data() + kv_offset + sizeof(uint64_t)), key_size_with_null - 1);
        m_current_value.value_offset = kv_offset + sizeof(uint64_t) + key_size_with_null;
        m_current_value.value_type = static_cast<Type>(m_buffer->m_data[m_current_value.value_offset]);

        m_stack[m_depth].key_index++;
        if (current_node.child_offsets[m_stack[m_depth].key_index] != 0) {
             m_depth++;
             m_stack[m_depth] = {current_node.child_offsets[m_stack[m_depth-1].key_index], 0};
             find_first();
        }
    }

} // namespace lite3
