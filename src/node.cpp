#include "node.hpp"
#include "buffer.hpp"
#include <cstring>

namespace lite3 {

    namespace {
        // Constants from lite3.c for bit manipulation
        constexpr uint32_t NODE_TYPE_SHIFT = 0;
        constexpr uint32_t NODE_TYPE_MASK = (1 << 8) - 1;  // 8 LSB

        constexpr uint32_t NODE_GEN_SHIFT = 8;
        constexpr uint32_t NODE_GEN_MASK = ~((1 << 8) - 1); // 24 MSB

        constexpr uint32_t NODE_KEY_COUNT_SHIFT = 0;
        constexpr uint32_t NODE_KEY_COUNT_MASK = (1 << 3) - 1; // 3 LSB for key_count up to 7

        constexpr uint32_t NODE_SIZE_SHIFT = 6;
        constexpr uint32_t NODE_SIZE_MASK = ~((1 << 6) - 1); // 26 MSB
    }

    void Node::read(const Buffer& buffer, size_t offset) {
        const auto* data_ptr = buffer.m_data.data() + offset;

        uint32_t gen_type;
        std::memcpy(&gen_type, data_ptr, sizeof(gen_type));
        generation = (gen_type & NODE_GEN_MASK) >> NODE_GEN_SHIFT;
        type = static_cast<Type>(gen_type & NODE_TYPE_MASK);

        std::memcpy(hashes.data(), data_ptr + 4, sizeof(hashes));

        uint32_t size_kc;
        std::memcpy(&size_kc, data_ptr + 32, sizeof(size_kc));
        size = (size_kc & NODE_SIZE_MASK) >> NODE_SIZE_SHIFT;
        key_count = (size_kc & NODE_KEY_COUNT_MASK) >> NODE_KEY_COUNT_SHIFT;

        std::memcpy(kv_offsets.data(), data_ptr + 36, sizeof(kv_offsets));
        std::memcpy(child_offsets.data(), data_ptr + 64, sizeof(child_offsets));
    }

    void Node::write(Buffer& buffer, size_t offset) const {
        if (buffer.m_data.size() < offset + config::node_size) {
            buffer.m_data.resize(offset + config::node_size);
        }
        auto* data_ptr = buffer.m_data.data() + offset;

        uint32_t gen_type = (generation << NODE_GEN_SHIFT) | (static_cast<uint8_t>(type) << NODE_TYPE_SHIFT);
        std::memcpy(data_ptr, &gen_type, sizeof(gen_type));

        std::memcpy(data_ptr + 4, hashes.data(), sizeof(hashes));

        uint32_t size_kc = (size << NODE_SIZE_SHIFT) | (key_count << NODE_KEY_COUNT_SHIFT);
        std::memcpy(data_ptr + 32, &size_kc, sizeof(size_kc));

        std::memcpy(data_ptr + 36, kv_offsets.data(), sizeof(kv_offsets));
        std::memcpy(data_ptr + 64, child_offsets.data(), sizeof(child_offsets));
    }

    // A helper struct to get offsets for memcpy, matching the C struct layout
    struct c_node {
        uint32_t gen_type;
        uint32_t hashes[config::node_key_count];
        uint32_t size_kc;
        uint32_t kv_ofs[config::node_key_count];
        uint32_t child_ofs[config::node_key_count + 1];
    };

} // namespace lite3


