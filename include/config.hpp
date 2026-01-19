#ifndef LITE3CPP_CONFIG_HPP
#define LITE3CPP_CONFIG_HPP

#include <cstddef> // for size_t

namespace lite3cpp {
namespace config {
constexpr size_t node_key_count = 7;
constexpr size_t node_key_count_min = node_key_count / 2;
constexpr size_t node_key_count_max = node_key_count;
constexpr size_t node_size = 96; // 1.5 cache lines
constexpr size_t tree_height_max = 9;
constexpr size_t node_alignment = 4;
} // namespace config
} // namespace lite3cpp

#ifndef LITE3CPP_JSON
#define LITE3CPP_JSON
#endif

// Bitmask definitions for packed layout - MUST MATCH lite3.c 96-byte
// configuration
#define NODE_GEN_MASK 0xFFFFFF00
#define NODE_GEN_SHIFT 8
#define NODE_TYPE_MASK 0x000000FF
#define NODE_TYPE_SHIFT 0

#define NODE_SIZE_MASK 0xFFFFFFC0
#define NODE_SIZE_SHIFT 6
#define NODE_KEY_COUNT_MASK                                                    \
  0x00000007 // 3 bits for 0-7 keys (matches lite3.c 96-byte config)
#define NODE_KEY_COUNT_SHIFT 0

#endif // LITE3CPP_CONFIG_HPP
