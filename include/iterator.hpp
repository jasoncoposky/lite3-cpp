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
        Iterator(const Buffer* buffer, size_t ofs, size_t node_offset);

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

        bool operator==(const Iterator& other) const;
        bool operator!=(const Iterator& other) const;

    private:
        const Buffer* m_buffer;
        
        struct
        {
            size_t offset;
            int key_index;
        } m_stack[config::tree_height_max + 1];
        int m_depth;

        value_type m_current_value;

        void find_first();
        void find_next();
    };

} // namespace lite3cpp

#endif // LITE3CPP_ITERATOR_HPP
