#ifndef LITE3CPP_PACKED_NODE_LAYOUT_HPP
#define LITE3CPP_PACKED_NODE_LAYOUT_HPP

#include <cstddef>
#include <cstdint>
#include "config.hpp"

namespace lite3cpp {

#pragma pack(push, 1)
    struct PackedNodeLayout {
        uint32_t gen_type;
        uint32_t hashes[config::node_key_count];
        uint32_t size_kc;
        union {
            uint32_t kv_ofs[config::node_key_count];
            uint32_t kv_offsets[config::node_key_count];
        };
        union {
            uint32_t child_ofs[config::node_key_count + 1];
            uint32_t child_offsets[config::node_key_count + 1];
        };
    };
#pragma pack(pop)

    static_assert(sizeof(PackedNodeLayout) == config::node_size,
                  "PackedNodeLayout size mismatch");

    constexpr size_t key_tag_byte_count(size_t klen_with_null) noexcept {
        if (klen_with_null < (1u << 6)) return 1;
        if (klen_with_null < (1u << 14)) return 2;
        if (klen_with_null < (1u << 22)) return 3;
        return 4;
    }

    inline size_t encode_key_tag(uint8_t* dst, size_t key_len) noexcept {
        const uint32_t klen = static_cast<uint32_t>(key_len + 1);
        const size_t tb = key_tag_byte_count(klen);
        const uint32_t raw = (klen << 2) | static_cast<uint32_t>(tb - 1);
        for (size_t b = 0; b < tb; ++b) {
            dst[b] = static_cast<uint8_t>((raw >> (b * 8)) & 0xFFu);
        }
        return tb;
    }

    inline bool decode_key_tag(const uint8_t* base, size_t bound, size_t vo,
                               uint32_t& klen_with_null, size_t& tag_bytes) noexcept {
        if (vo >= bound) return false;
        const size_t tb = static_cast<size_t>(base[vo] & 0x03u) + 1u;
        if (vo + tb > bound) return false;
        uint32_t raw = 0;
        for (size_t b = 0; b < tb; ++b) {
            raw |= static_cast<uint32_t>(base[vo + b]) << (b * 8);
        }
        const uint32_t klen = raw >> 2;
        if (klen == 0 || vo + tb + klen > bound) return false;
        klen_with_null = klen;
        tag_bytes = tb;
        return true;
    }

} // namespace lite3cpp

#endif // LITE3CPP_PACKED_NODE_LAYOUT_HPP
