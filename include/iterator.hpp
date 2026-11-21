#ifndef LITE3CPP_ITERATOR_HPP
#define LITE3CPP_ITERATOR_HPP

#include "node.hpp"
#include <string_view>
#include <cstddef>
#include <vector>

namespace lite3cpp {

    class Buffer;

    class Iterator {
    public:
        Iterator(const Buffer* buffer, size_t ofs, size_t node_offset, uint32_t initial_buffer_generation);

        Iterator& operator++();
        // TODO: post-increment
        // Iterator operator++(int);

        struct value_type {
            std::string_view key;
            size_t value_offset;
            Type value_type;
        };

        const value_type& operator*() const;
        const value_type* operator->() const;
        std::string_view key() const;

        bool operator==(const Iterator& other) const {
            if (m_buffer != other.m_buffer) return false;
            if (!m_buffer) return true;
            return equals_non_end(other);
        }
        bool operator!=(const Iterator& other) const {
            return !(*this == other);
        }

    private:
        bool equals_non_end(const Iterator& other) const;
        const Buffer* m_buffer;
        const uint8_t* m_initial_data = nullptr;
        size_t m_initial_bound_size = 0;
        uint32_t m_initial_buffer_generation;
        uint32_t m_initial_node_generation = 0;

        struct
        {
            size_t offset;
            int key_index;
        } m_stack[config::tree_height_max + 1];
        int m_depth;
        bool m_is_array = false;

        value_type m_current_value{};

        bool is_modified() const noexcept;
        void find_first();
        void find_next();
    };

} // namespace lite3cpp

#endif // LITE3CPP_ITERATOR_HPP
