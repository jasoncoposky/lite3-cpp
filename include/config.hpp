#ifndef LITE3_CONFIG_HPP
#define LITE3_CONFIG_HPP

namespace lite3::config {
    constexpr size_t node_key_count = 7;
    constexpr size_t node_size = 96; // 1.5 cache lines
    constexpr size_t tree_height_max = 9;
    constexpr size_t node_alignment = 4;
    #define LITE3_JSON
}

#endif // LITE3_CONFIG_HPP
