#ifndef LITE3CPP_PACKED_NODE_LAYOUT_HPP
#define LITE3CPP_PACKED_NODE_LAYOUT_HPP

#include <cstdint>
#include <array>
#include "config.hpp"

namespace lite3cpp {

    // Constants from lite3.c for bit manipulation
    constexpr uint32_t NODE_TYPE_SHIFT = 0;
    constexpr uint32_t NODE_TYPE_MASK = (1 << 8) - 1;  // 8 LSB

    constexpr uint32_t NODE_GEN_SHIFT = 8;
    constexpr uint32_t NODE_GEN_MASK = ~((1 << 8) - 1); // 24 MSB

    constexpr uint32_t NODE_KEY_COUNT_SHIFT = 0;
    constexpr uint32_t NODE_KEY_COUNT_MASK = (1 << 6) - 1; // 6 LSB for key_count up to 63

    constexpr uint32_t NODE_SIZE_SHIFT = 6;
    constexpr uint32_t NODE_SIZE_MASK = ~((1 << 6) - 1); // 26 MSB

#pragma pack(push, 1)
    struct PackedNodeLayout {
        uint32_t gen_type;
        uint32_t hashes[config::node_key_count];
        uint32_t size_kc;
        uint32_t kv_offsets[config::node_key_count];
        uint32_t child_offsets[config::node_key_count + 1];
    };
#pragma pack(pop)

} // namespace lite3cpp

#endif // LITE3CPP_PACKED_NODE_LAYOUT_HPP
