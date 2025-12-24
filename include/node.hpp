#ifndef LITE3_NODE_HPP
#define LITE3_NODE_HPP

#include <cstdint>
#include <array>
#include "config.hpp"

namespace lite3 {

    // Forward declaration
    class Buffer;

    enum class Type : uint8_t {
        Null,
        Bool,
        Int64,
        Float64,
        Bytes,
        String,
        Object,
        Array,
        Invalid,
        Count
    };

class Iterator;

    struct Node {
        uint32_t generation;
        Type type;
        uint32_t size;
        uint8_t key_count;
        std::array<uint32_t, config::node_key_count> hashes;
        std::array<uint32_t, config::node_key_count> kv_offsets;
        std::array<uint32_t, config::node_key_count + 1> child_offsets;

        // Functions to read/write from/to a buffer
        void read(const Buffer& buffer, size_t offset);
        void write(Buffer& buffer, size_t offset) const;
    };

} // namespace lite3

#endif // LITE3_NODE_HPP
