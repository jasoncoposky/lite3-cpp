#ifndef LITE3_BUFFER_HPP
#define LITE3_BUFFER_HPP

#include <vector>
#include <cstddef>
#include <string>
#include <string_view>
#include <cstdint>
#include <span>

#include "config.hpp"
#include "node.hpp"
#include "utils/hash.hpp"
#include "iterator.hpp"

namespace lite3 {

    class Buffer {
    public:
        Buffer();
        explicit Buffer(size_t initial_size);

        void init_object();
        void init_array();

        void set_null(size_t ofs, std::string_view key);
        void set_bool(size_t ofs, std::string_view key, bool value);
        void set_i64(size_t ofs, std::string_view key, int64_t value);
        void set_f64(size_t ofs, std::string_view key, double value);
        void set_str(size_t ofs, std::string_view key, std::string_view value);
        void set_bytes(size_t ofs, std::string_view key, std::span<const std::byte> value);
        size_t set_obj(size_t ofs, std::string_view key);
        size_t set_arr(size_t ofs, std::string_view key);

        void arr_append_null(size_t ofs);
        void arr_append_bool(size_t ofs, bool value);
        void arr_append_i64(size_t ofs, int64_t value);
        void arr_append_f64(size_t ofs, double value);
        void arr_append_str(size_t ofs, std::string_view value);
        void arr_append_bytes(size_t ofs, std::span<const std::byte> value);
        size_t arr_append_obj(size_t ofs);
        size_t arr_append_arr(size_t ofs);

        bool get_bool(size_t ofs, std::string_view key) const;
        int64_t get_i64(size_t ofs, std::string_view key) const;
        double get_f64(size_t ofs, std::string_view key) const;
        std::string_view get_str(size_t ofs, std::string_view key) const;
        std::span<const std::byte> get_bytes(size_t ofs, std::string_view key) const;
        size_t get_obj(size_t ofs, std::string_view key) const;
        size_t get_arr(size_t ofs, std::string_view key) const;

        bool arr_get_bool(size_t ofs, uint32_t index) const;
        int64_t arr_get_i64(size_t ofs, uint32_t index) const;
        double arr_get_f64(size_t ofs, uint32_t index) const;
        std::string_view arr_get_str(size_t ofs, uint32_t index) const;
        std::span<const std::byte> arr_get_bytes(size_t ofs, uint32_t index) const;
        size_t arr_get_obj(size_t ofs, uint32_t index) const;
        size_t arr_get_arr(size_t ofs, uint32_t index) const;
        Type arr_get_type(size_t ofs, uint32_t index) const;

        Iterator begin(size_t ofs) const;
        Iterator end(size_t ofs) const;

    private:
        friend class Node;
        friend class Iterator;
        friend class json;
        
        size_t set_impl(size_t ofs, std::string_view key, uint32_t hash, size_t val_len, const void* val_ptr, Type type);
        void arr_append_impl(size_t ofs, size_t val_len, const void* val_ptr, Type type);
        const std::byte* get_impl(size_t ofs, std::string_view key, uint32_t hash, Type& type) const;
        const std::byte* arr_get_impl(size_t ofs, uint32_t index, Type& type) const;
        void split_child(Node& parent, int child_index, Node& full_child, size_t full_child_offset);
        size_t append_kv(std::string_view key, Type type, const void* val_ptr, size_t val_len);

    public:
        std::vector<std::byte> m_data;
        size_t m_used_size;
    };

} // namespace lite3

#endif // LITE3_BUFFER_HPP