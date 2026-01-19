#ifndef LITE3CPP_NODE_HPP
#define LITE3CPP_NODE_HPP

#include "config.hpp"
#include <cstdint>

namespace lite3cpp {

enum class Type : uint8_t {
  Null = 0,
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

inline const size_t type_sizes[] = {
    0,                                    // Null
    1,                                    // Bool
    8,                                    // Int64
    8,                                    // Float64
    4,                                    // Bytes
    4,                                    // String
    config::node_size - sizeof(uint64_t), // Object
    config::node_size - sizeof(uint64_t), // Array
    0                                     // Invalid
};

struct PackedNodeLayout {
  uint32_t gen_type;
  uint32_t hashes[config::node_key_count];
  uint32_t size_kc;
  uint32_t kv_ofs[config::node_key_count];
  uint32_t child_ofs[config::node_key_count + 1];
};

static_assert(sizeof(PackedNodeLayout) == config::node_size,
              "PackedNodeLayout size mismatch");

class NodeView {
public:
  const PackedNodeLayout *packed;

  NodeView(const PackedNodeLayout *p) : packed(p) {}

  uint32_t generation() const {
    return (packed->gen_type & NODE_GEN_MASK) >> NODE_GEN_SHIFT;
  }

  Type type() const {
    return static_cast<Type>((packed->gen_type & NODE_TYPE_MASK) >>
                             NODE_TYPE_SHIFT);
  }

  uint32_t size() const {
    return (packed->size_kc & NODE_SIZE_MASK) >> NODE_SIZE_SHIFT;
  }

  uint32_t key_count() const {
    return (packed->size_kc & NODE_KEY_COUNT_MASK) >> NODE_KEY_COUNT_SHIFT;
  }

  uint32_t get_hash(int i) const { return packed->hashes[i]; }

  uint32_t get_kv_offset(int i) const { return packed->kv_ofs[i]; }

  uint32_t get_child_offset(int i) const { return packed->child_ofs[i]; }
};

class MutableNodeView {
public:
  PackedNodeLayout *packed;

  MutableNodeView(PackedNodeLayout *p) : packed(p) {}

  operator NodeView() const { return NodeView(packed); }

  uint32_t generation() const {
    return (packed->gen_type & NODE_GEN_MASK) >> NODE_GEN_SHIFT;
  }

  void set_gen_type(uint32_t gen, Type type) {
    packed->gen_type =
        (gen << NODE_GEN_SHIFT) | (static_cast<uint8_t>(type) & NODE_TYPE_MASK);
  }

  Type type() const {
    return static_cast<Type>((packed->gen_type & NODE_TYPE_MASK) >>
                             NODE_TYPE_SHIFT);
  }

  uint32_t size() const {
    return (packed->size_kc & NODE_SIZE_MASK) >> NODE_SIZE_SHIFT;
  }

  void set_size(uint32_t size) {
    packed->size_kc = (packed->size_kc & ~NODE_SIZE_MASK) |
                      ((size << NODE_SIZE_SHIFT) & NODE_SIZE_MASK);
  }

  void set_size_kc(uint32_t size, uint32_t key_count) {
    packed->size_kc =
        (size << NODE_SIZE_SHIFT) | (key_count & NODE_KEY_COUNT_MASK);
  }

  uint32_t key_count() const {
    return (packed->size_kc & NODE_KEY_COUNT_MASK) >> NODE_KEY_COUNT_SHIFT;
  }

  void set_key_count(uint32_t key_count) {
    packed->size_kc = (packed->size_kc & ~NODE_KEY_COUNT_MASK) |
                      (key_count & NODE_KEY_COUNT_MASK);
  }

  void set_hash(int i, uint32_t hash) { packed->hashes[i] = hash; }

  uint32_t get_hash(int i) const { return packed->hashes[i]; }

  void set_kv_offset(int i, uint32_t offset) { packed->kv_ofs[i] = offset; }

  uint32_t get_kv_offset(int i) const { return packed->kv_ofs[i]; }

  void set_child_offset(int i, uint32_t offset) {
    packed->child_ofs[i] = offset;
  }

  uint32_t get_child_offset(int i) const { return packed->child_ofs[i]; }
};

} // namespace lite3cpp

#endif // LITE3CPP_NODE_HPP
