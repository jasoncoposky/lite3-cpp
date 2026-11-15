#include "buffer.hpp"
#include "exception.hpp"
#include "node.hpp"
#include "observability.hpp"
#include "utils/hash.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>

namespace lite3cpp {

// Helper for B-tree comparison (Hash -> Key)
static int compare_node_key(const uint8_t *base, size_t data_size,
                            const NodeView &node, int idx, uint32_t hash,
                            std::string_view key, bool is_arr,
                            bool *corrupt_tag = nullptr) noexcept {
  uint32_t nh = node.get_hash(idx);
  if (nh < hash) {
    return -1;
  }
  if (nh > hash) {
    return 1;
  }
  if (is_arr)
    return 0; // Arrays use unique index as hash

  size_t vo = node.get_kv_offset(idx);
  uint32_t klen = 0;
  size_t tag_bytes = 0;
  if (!decode_key_tag(base, data_size, vo, klen, tag_bytes)) {
    if (corrupt_tag != nullptr) {
      *corrupt_tag = true;
    }
    return -1;
  }
  size_t ksz = klen - 1;

  std::string_view existing(
      reinterpret_cast<const char *>(base + vo + tag_bytes), ksz);
  return existing.compare(key);
}

struct ScopedMetric {
  [[maybe_unused]] std::string_view op;
#ifndef LITE3CPP_DISABLE_OBSERVABILITY
  IMetrics *metrics = nullptr;
  std::chrono::high_resolution_clock::time_point start{};
#endif
  ScopedMetric(std::string_view o) noexcept : op(o) {
#ifndef LITE3CPP_DISABLE_OBSERVABILITY
    IMetrics *m = g_metrics.load(std::memory_order_relaxed);
    if (m != nullptr) [[unlikely]] {
      std::atomic_thread_fence(std::memory_order_acquire);
      metrics = m;
      start = std::chrono::high_resolution_clock::now();
    }
#endif
  }
  ~ScopedMetric() noexcept {
#ifndef LITE3CPP_DISABLE_OBSERVABILITY
    if (metrics != nullptr) [[unlikely]] {
      try {
        auto end = std::chrono::high_resolution_clock::now();
        double dur = std::chrono::duration<double>(end - start).count();
        metrics->record_latency(op, dur);
        metrics->increment_operation_count(op, "ok");
      } catch (...) {
      }
    }
#endif
  }
};

Buffer::Buffer(size_t initial_size) : m_ext_data(nullptr), m_used_size(0) {
  m_data.resize(initial_size);
}

Buffer::Buffer(std::vector<uint8_t> data)
    : m_data(std::move(data)), m_ext_data(nullptr), m_used_size(m_data.size()) {}

Buffer::Buffer(const uint8_t *ptr, size_t len)
    : m_ext_data(nullptr), m_used_size((ptr && len > 0) ? len : 0) {
  if (ptr && len > 0) {
    m_data.assign(ptr, ptr + len);
  }
}

Buffer::Buffer(std::string_view sv)
    : m_data(reinterpret_cast<const uint8_t *>(sv.data()),
             reinterpret_cast<const uint8_t *>(sv.data()) + sv.size()),
      m_ext_data(nullptr),
      m_used_size(sv.size()) {}

Buffer::Buffer(Buffer &&other) noexcept
    : m_data(std::move(other.m_data)),
      m_ext_data(other.m_ext_data),
      m_used_size(other.m_used_size) {
  other.m_ext_data = nullptr;
  other.m_used_size = 0;
}

Buffer &Buffer::operator=(Buffer &&other) noexcept {
  if (this != &other) {
    m_data = std::move(other.m_data);
    m_ext_data = other.m_ext_data;
    m_used_size = other.m_used_size;
    other.m_ext_data = nullptr;
    other.m_used_size = 0;
  }
  return *this;
}

void Buffer::materialize_if_view() {
  if (m_ext_data != nullptr) {
    m_data.assign(m_ext_data, m_ext_data + m_used_size);
    m_ext_data = nullptr;
  }
}

std::vector<uint8_t> Buffer::release() {
  if (m_ext_data != nullptr) {
    std::vector<uint8_t> out(m_ext_data, m_ext_data + m_used_size);
    m_data.clear();
    m_ext_data = nullptr;
    m_used_size = 0;
    return out;
  }
  if (m_data.size() > m_used_size) {
    m_data.resize(m_used_size);
  }
  std::vector<uint8_t> out = std::move(m_data);
  m_used_size = 0;
  return out;
}

void Buffer::ensure_capacity(size_t required_bytes) {
  if (required_bytes > SIZE_MAX - m_used_size) {
    throw exception("Buffer size overflow");
  }
  const size_t needed = m_used_size + required_bytes;

  if (m_ext_data != nullptr) {
    size_t new_size = (m_used_size <= SIZE_MAX / 2)
                          ? std::max(m_used_size * 2, needed)
                          : needed;
    if (new_size < config::node_size) {
      new_size = config::node_size;
    }
    const uint8_t *ext = m_ext_data;
    const size_t used = m_used_size;
    m_data.resize(new_size);
    if (used > 0) {
      std::memcpy(m_data.data(), ext, used);
    }
    m_ext_data = nullptr;
    return;
  }

  if (needed > m_data.size()) {
    const size_t target_cap = m_data.capacity();
    const size_t doubled =
        (m_data.size() <= SIZE_MAX / 2) ? (m_data.size() * 2) : SIZE_MAX;
    size_t new_size = std::max(doubled, needed);
    if (new_size < target_cap && target_cap >= needed) {
      new_size = target_cap;
    }
    if (new_size < config::node_size)
      new_size = config::node_size;
    m_data.resize(new_size);
  }
}

void Buffer::init_structure(Type type) {
  m_ext_data = nullptr;
  m_used_size = 0;
  ensure_capacity(config::node_size);
  std::memset(m_data.data(), 0, config::node_size);

  MutableNodeView root(reinterpret_cast<PackedNodeLayout *>(m_data.data()));
  root.set_gen_type(1, type);

  m_used_size = config::node_size;
}

void Buffer::init_object() { init_structure(Type::Object); }

void Buffer::init_array() { init_structure(Type::Array); }

// Internal iterative set implementation
size_t Buffer::set_impl(size_t ofs, std::string_view key, uint32_t key_hash,
                        size_t val_len, const void *val_ptr, Type type,
                        bool is_append) {
  ScopedMetric sm("set");
  if (m_used_size < config::node_size || ofs > m_used_size - config::node_size) {
    throw exception("Invalid offset");
  }
  if (!is_append && key.size() >= (1u << 30)) {
    throw exception("Key size overflow");
  }
  if ((type == Type::String || type == Type::Bytes) && val_len > UINT32_MAX) {
    throw exception("Value size overflow");
  }

  auto ptr_in_owned_buffer = [this](const void *p, size_t &out_ofs) noexcept -> bool {
    if (p == nullptr || m_ext_data != nullptr || m_data.empty()) {
      return false;
    }
    const auto base_addr = reinterpret_cast<std::uintptr_t>(m_data.data());
    const auto end_addr = base_addr + m_data.size();
    const auto p_addr = reinterpret_cast<std::uintptr_t>(p);
    if (p_addr >= base_addr && p_addr < end_addr) {
      out_ofs = static_cast<size_t>(p_addr - base_addr);
      return true;
    }
    return false;
  };

  size_t key_alias_ofs = 0;
  const bool key_aliases_owned =
      !is_append && !key.empty() && ptr_in_owned_buffer(key.data(), key_alias_ofs);

  size_t val_alias_ofs = 0;
  const bool val_aliases_owned =
      (val_len > 0 && val_ptr != nullptr) && ptr_in_owned_buffer(val_ptr, val_alias_ofs);

  auto rebase_aliases = [&]() noexcept {
    if (key_aliases_owned) {
      key = std::string_view(
          reinterpret_cast<const char *>(m_data.data() + key_alias_ofs),
          key.size());
    }
    if (val_aliases_owned) {
      val_ptr = m_data.data() + val_alias_ofs;
    }
  };

  if (m_ext_data != nullptr) {
    ensure_capacity(0);
  }

  size_t key_tag_size = is_append ? 0 : key_tag_byte_count(key.size() + 1);

  size_t parent_ofs = SIZE_MAX;
  size_t node_ofs = ofs;

  // Path stack for ancestor tracking (allows root growth beyond tree_height_max)
  constexpr int max_path_depth = static_cast<int>(config::tree_height_max) + 4;
  size_t path[max_path_depth];
  int path_depth = 0;

  while (true) {
    if (node_ofs > m_used_size - config::node_size) {
      throw exception("Corrupt B-tree node offset");
    }
    if (path_depth < 0 ||
        path_depth > static_cast<int>(config::tree_height_max) + 2 ||
        path_depth >= max_path_depth) {
      throw exception("Corrupt B-tree: max height exceeded");
    }
    path[path_depth++] = node_ofs;

    // Re-acquire pointers
    auto *node_ptr =
        reinterpret_cast<PackedNodeLayout *>(m_data.data() + node_ofs);
    MutableNodeView node(node_ptr);

    node.set_gen_type(node.generation() + 1, node.type());

    // Check Split
    if (node.key_count() >= config::node_key_count_max) {
      log_if_enabled(LogLevel::Info, "Node is full, splitting", "set_impl",
                     std::chrono::microseconds(0), 0, "");
      if (m_used_size > SIZE_MAX - config::node_alignment) {
        throw exception("Buffer size overflow");
      }
      size_t next_aligned = (m_used_size + config::node_alignment - 1) &
                            ~(config::node_alignment - 1);
      size_t space_needed = (parent_ofs == SIZE_MAX) ? (2 * config::node_size)
                                                     : config::node_size;
      ensure_capacity(space_needed + (next_aligned - m_used_size) +
                      128); // +slack
      rebase_aliases();

      if (next_aligned > m_used_size) {
        std::memset(m_data.data() + m_used_size, 0, next_aligned - m_used_size);
      }
      m_used_size = next_aligned;

      // RE-ACQUIRE
      node_ptr = reinterpret_cast<PackedNodeLayout *>(m_data.data() + node_ofs);
      node = MutableNodeView(node_ptr);
      MutableNodeView parent(nullptr);
      if (parent_ofs != SIZE_MAX) {
        parent = MutableNodeView(
            reinterpret_cast<PackedNodeLayout *>(m_data.data() + parent_ofs));
      }

      if (parent_ofs == SIZE_MAX) { // Root Split
        size_t moves_to_ofs = m_used_size;
        m_used_size += config::node_size;
        std::memcpy(m_data.data() + moves_to_ofs, node_ptr, config::node_size);

        // Reset old root as new parent
        auto root_type = node.type();
        std::memset(node_ptr, 0, config::node_size);
        node.set_gen_type(1, root_type);
        node.set_key_count(0);
        node.set_child_offset(0, static_cast<uint32_t>(moves_to_ofs));
        MutableNodeView moved(
            reinterpret_cast<PackedNodeLayout *>(m_data.data() + moves_to_ofs));
        node.set_size_kc(moved.size(), 0);

        parent_ofs = node_ofs;
        node_ofs = moves_to_ofs;
        // Keep parent_ofs at path[path_depth - 1]; next iteration pushes moves_to_ofs
        path[path_depth - 1] = parent_ofs;
        continue;
      }

      // Normal Split
      int i_in_parent = -1;
      for (int k = 0; k <= static_cast<int>(parent.key_count()); ++k) {
        if (parent.get_child_offset(k) == node_ofs) {
          i_in_parent = k;
          break;
        }
      }
      if (i_in_parent < 0 ||
          parent.key_count() >= config::node_key_count_max) {
        throw exception("Corrupt B-tree parent link");
      }

      size_t sibling_ofs = m_used_size;
      m_used_size += config::node_size;
      std::memset(m_data.data() + sibling_ofs, 0, config::node_size);
      MutableNodeView sibling(
          reinterpret_cast<PackedNodeLayout *>(m_data.data() + sibling_ofs));

      sibling.set_gen_type(node.generation(), node.type());

      int mid = static_cast<int>(config::node_key_count_min);
      // Move keys [mid+1..end] to sibling
      int move_count = static_cast<int>(node.key_count()) - (mid + 1);

      bool is_leaf = (node.get_child_offset(0) == 0);
      if (!is_leaf) {
        sibling.set_child_offset(0, node.get_child_offset(mid + 1));
      }
      for (int j = 0; j < move_count; ++j) {
        sibling.set_hash(j, node.get_hash(mid + 1 + j));
        sibling.set_kv_offset(j, node.get_kv_offset(mid + 1 + j));
        if (!is_leaf) {
          sibling.set_child_offset(j + 1, node.get_child_offset(mid + 2 + j));
        }
      }
      sibling.set_size_kc(0, static_cast<uint32_t>(move_count));

      // Insert median into parent
      for (int j = static_cast<int>(parent.key_count()); j > i_in_parent; j--) {
        parent.set_hash(j, parent.get_hash(j - 1));
        parent.set_kv_offset(j, parent.get_kv_offset(j - 1));
        parent.set_child_offset(j + 1, parent.get_child_offset(j));
      }
      parent.set_hash(i_in_parent, node.get_hash(mid));
      parent.set_kv_offset(i_in_parent, node.get_kv_offset(mid));
      parent.set_child_offset(i_in_parent + 1,
                              static_cast<uint32_t>(sibling_ofs));
      parent.set_size_kc(parent.size(), parent.key_count() + 1);

      node.set_key_count(static_cast<uint32_t>(mid));
      if (is_leaf) {
        for (size_t k = 0; k <= config::node_key_count; ++k) {
          node.set_child_offset(static_cast<int>(k), 0);
          sibling.set_child_offset(static_cast<int>(k), 0);
        }
      }

      bool corrupt_med = false;
      int cmp_med = compare_node_key(m_data.data(), bound_size(), parent,
                                     i_in_parent, key_hash, key, is_append,
                                     &corrupt_med);
      if (corrupt_med) {
        throw exception("Corrupted key tag in buffer");
      }
      if (cmp_med < 0) {
        node_ofs = sibling_ofs;
        path_depth--;
      } else if (cmp_med == 0) {
        path_depth -= 2;
        node_ofs = parent_ofs;
        parent_ofs = (path_depth > 0) ? path[path_depth - 1] : SIZE_MAX;
      } else {
        path_depth--;
      }
      continue; // Restart loop
    }

    // Search (at most 1 compare_node_key call per inspected slot)
    int i = 0;
    int count = static_cast<int>(node.key_count());
    int cmp = 1;
    bool corrupt_tag = false;
    while (i < count) {
      cmp = compare_node_key(m_data.data(), bound_size(), node, i, key_hash,
                             key, is_append, &corrupt_tag);
      if (corrupt_tag) {
        throw exception("Corrupted key tag in buffer");
      }
      if (cmp < 0) {
        i++;
      } else {
        break;
      }
    }

    if (cmp == 0) {
      // Calculate new size requirements early for both paths
      size_t klen = is_append ? 0 : (key.size() + key_tag_size + 1);

      size_t val_total_len = 1 + val_len; // Type+Data
      if (type == Type::String || type == Type::Bytes)
        val_total_len += 4;
      if (type == Type::String)
        val_total_len++;

      // === OPTIMIZATION: Check for In-Place Update ===
      size_t kv_ofs = node.get_kv_offset(i);

      // Calculate existing value offset
      size_t vo = kv_ofs;
      if (!is_append) {
        uint32_t existing_klen = 0;
        size_t existing_tag_bytes = 0;
        if (!decode_key_tag(m_data.data(), bound_size(), vo, existing_klen,
                            existing_tag_bytes)) {
          throw exception("Corrupted key tag in buffer");
        }
        vo += existing_tag_bytes + existing_klen;
      }

      if (vo >= bound_size()) {
        throw exception("Corrupted value offset in buffer");
      }

      // Check existing value size
      Type existing_type = static_cast<Type>(m_data[vo]);
      size_t existing_vlen = 0;
      size_t existing_total_len = 1; // Type

      if (existing_type == Type::String || existing_type == Type::Bytes) {
        if (bound_size() - vo < 5) {
          throw exception("Corrupted value length in buffer");
        }
        uint32_t raw_vlen = 0;
        std::memcpy(&raw_vlen, m_data.data() + vo + 1, 4);
        existing_vlen = raw_vlen;
        existing_total_len += 4 + existing_vlen; // Type + Size + Data
        if (existing_type == Type::String)
          existing_total_len++; // Null
      } else {
        // Fixed types
        if (existing_type == Type::Bool)
          existing_vlen = 1;
        else if (existing_type == Type::Int64 || existing_type == Type::Float64)
          existing_vlen = 8;
        else if (existing_type == Type::Null)
          existing_vlen = 0;
        existing_total_len += existing_vlen;
      }

      if (existing_type != Type::Object && existing_type != Type::Array &&
          existing_total_len > bound_size() - vo) {
        throw exception("Corrupted value in buffer");
      }

      if (type != Type::Object && type != Type::Array &&
          existing_type != Type::Object && existing_type != Type::Array &&
          existing_total_len == val_total_len) {
        // Overwrite in place!
        size_t vstart = vo + 1;                  // Skip Type
        m_data[vo] = static_cast<uint8_t>(type); // Update Type

        if (type == Type::String || type == Type::Bytes) {
          uint8_t *payload_dst = m_data.data() + vstart + 4;
          if (val_len && val_ptr != nullptr && payload_dst != val_ptr)
            std::memmove(payload_dst, val_ptr, val_len);
          uint32_t sz = static_cast<uint32_t>(val_len);
          std::memcpy(m_data.data() + vstart, &sz, 4);
          if (type == Type::String)
            payload_dst[val_len] = 0;
        } else {
          uint8_t *payload_dst = m_data.data() + vstart;
          if (val_len && val_ptr != nullptr && payload_dst != val_ptr)
            std::memmove(payload_dst, val_ptr, val_len);
        }
        return vo;
      }
      // ===============================================

      // Fallback: Append
      if (klen > SIZE_MAX - val_total_len) {
        throw exception("Buffer size overflow");
      }
      ensure_capacity(klen + val_total_len);
      rebase_aliases();
      // Re-acquire pointers after resize
      node_ptr = reinterpret_cast<PackedNodeLayout *>(m_data.data() + node_ofs);
      MutableNodeView(node_ptr).set_kv_offset(i, static_cast<uint32_t>(m_used_size));

      size_t start = m_used_size;
      m_used_size += klen + val_total_len;

      // Write Key
      if (!is_append) {
        size_t tb = encode_key_tag(m_data.data() + start, key.size());
        std::memcpy(m_data.data() + start + tb, key.data(), key.size());
        m_data[start + tb + key.size()] = 0; // Null terminator
      }

      size_t vstart = start + klen;
      m_data[vstart++] = static_cast<uint8_t>(type);
      if (type == Type::String || type == Type::Bytes) {
        uint32_t sz = static_cast<uint32_t>(val_len);
        std::memcpy(m_data.data() + vstart, &sz, 4);
        vstart += 4;
        if (val_len && val_ptr != nullptr)
          std::memcpy(m_data.data() + vstart, val_ptr, val_len);
        if (type == Type::String)
          m_data[vstart + val_len] = 0;
      } else {
        if (val_len && val_ptr != nullptr)
          std::memcpy(m_data.data() + vstart, val_ptr, val_len);
      }
      return m_used_size - val_total_len;
    }

    if (node.get_child_offset(0) != 0 && node.get_child_offset(i) != 0) {
      parent_ofs = node_ofs;
      node_ofs = node.get_child_offset(i);
      continue;
    } else {
      // Insert Leaf
      size_t klen = is_append ? 0 : (key.size() + key_tag_size + 1);
      size_t vlen = 1 + val_len;
      if (type == Type::String || type == Type::Bytes)
        vlen += 4;
      if (type == Type::String)
        vlen++;

      if (klen > SIZE_MAX - vlen) {
        throw exception("Buffer size overflow");
      }
      ensure_capacity(klen + vlen);
      rebase_aliases();
      node_ptr = reinterpret_cast<PackedNodeLayout *>(m_data.data() + node_ofs);
      MutableNodeView node_upd(node_ptr);

      size_t start = m_used_size;
      m_used_size += klen + vlen;

      // Write Key
      if (!is_append) {
        size_t tb = encode_key_tag(m_data.data() + start, key.size());
        std::memcpy(m_data.data() + start + tb, key.data(), key.size());
        m_data[start + tb + key.size()] = 0; // Null terminator
      }

      size_t vstart = start + klen;
      m_data[vstart++] = static_cast<uint8_t>(type);
      if (type == Type::String || type == Type::Bytes) {
        uint32_t sz = static_cast<uint32_t>(val_len);
        std::memcpy(m_data.data() + vstart, &sz, 4);
        vstart += 4;
        if (val_len && val_ptr != nullptr)
          std::memcpy(m_data.data() + vstart, val_ptr, val_len);
        if (type == Type::String)
          m_data[vstart + val_len] = 0;
      } else {
        if (val_len && val_ptr != nullptr)
          std::memcpy(m_data.data() + vstart, val_ptr, val_len);
      }

      // Shift
      for (int j = count; j > i; j--) {
        node_upd.set_hash(j, node_upd.get_hash(j - 1));
        node_upd.set_kv_offset(j, node_upd.get_kv_offset(j - 1));
      }
      node_upd.set_hash(i, key_hash);
      node_upd.set_kv_offset(i, static_cast<uint32_t>(start));
      node_upd.set_key_count(static_cast<uint32_t>(count + 1));

      return start + klen;
    }
  }
}

const std::byte *
Buffer::get_impl_noexcept(size_t ofs, std::string_view key, uint32_t hash,
                          Type &type, bool is_array_op,
                          bool *max_height_exceeded) const noexcept {
  ScopedMetric sm("get");
  const uint8_t *base = data();
  const size_t data_sz = bound_size();
  size_t node_ofs = ofs;
  size_t depth = 0;

  while (true) {
    if (base == nullptr || data_sz < sizeof(PackedNodeLayout) ||
        node_ofs > data_sz - sizeof(PackedNodeLayout)) {
      type = Type::Invalid;
      return nullptr;
    }
    if (depth > config::tree_height_max) {
      if (max_height_exceeded != nullptr) {
        *max_height_exceeded = true;
      }
      type = Type::Invalid;
      return nullptr;
    }

    NodeView node(
        reinterpret_cast<const PackedNodeLayout *>(base + node_ofs));
    if (node.type() != (is_array_op ? Type::Array : Type::Object)) {
      type = Type::Invalid;
      return nullptr;
    }

    int i = 0;
    int count = static_cast<int>(node.key_count());
    int cmp = 1;
    bool corrupt_tag = false;
    while (i < count) {
      cmp = compare_node_key(base, data_sz, node, i, hash, key, is_array_op,
                             &corrupt_tag);
      if (corrupt_tag) {
        type = Type::Invalid;
        return nullptr;
      }
      if (cmp < 0) {
        i++;
      } else {
        break;
      }
    }

    if (cmp == 0) {
      size_t vo = node.get_kv_offset(i);
      if (!is_array_op) {
        uint32_t klen = 0;
        size_t tag_bytes = 0;
        if (!decode_key_tag(base, data_sz, vo, klen, tag_bytes)) {
          type = Type::Invalid;
          return nullptr;
        }
        vo += tag_bytes + klen;
      }

      if (vo >= data_sz) {
        type = Type::Invalid;
        return nullptr;
      }
      const uint8_t raw_type = base[vo];
      if (raw_type >= static_cast<uint8_t>(Type::Invalid)) {
        type = Type::Invalid;
        return nullptr;
      }
      type = static_cast<Type>(raw_type);
      return reinterpret_cast<const std::byte *>(base + vo + 1);
    }

    if (node.get_child_offset(0) != 0 && node.get_child_offset(i) != 0) {
      node_ofs = node.get_child_offset(i);
      ++depth;
      continue;
    }

    type = Type::Invalid;
    return nullptr;
  }
}

const std::byte *Buffer::get_impl(size_t ofs, std::string_view key,
                                  uint32_t hash, Type &type,
                                  bool is_array_op) const {
  bool max_height_exceeded = false;
  const std::byte *p = get_impl_noexcept(ofs, key, hash, type, is_array_op,
                                         &max_height_exceeded);
  if (max_height_exceeded) {
    throw exception("Corrupt B-tree: max height exceeded");
  }
  return p;
}

void Buffer::set_null(size_t ofs, HashedKey key) {
  set_impl(ofs, key.key, key.hash, 0, nullptr, Type::Null);
}
void Buffer::set_i64(size_t ofs, HashedKey key, int64_t value) {
  set_impl(ofs, key.key, key.hash, sizeof(value), &value, Type::Int64);
}
void Buffer::set_f64(size_t ofs, HashedKey key, double value) {
  set_impl(ofs, key.key, key.hash, sizeof(value), &value, Type::Float64);
}
void Buffer::set_str(size_t ofs, HashedKey key, std::string_view value) {
  set_impl(ofs, key.key, key.hash, value.size(), value.data(), Type::String);
}
void Buffer::set_bool(size_t ofs, HashedKey key, bool value) {
  set_impl(ofs, key.key, key.hash, sizeof(value), &value, Type::Bool);
}
void Buffer::set_bytes(size_t ofs, HashedKey key,
                       std::span<const std::byte> value) {
  set_impl(ofs, key.key, key.hash, value.size(), value.data(), Type::Bytes);
}
std::span<std::byte> Buffer::set_bytes_uninitialized(size_t ofs, HashedKey key,
                                                     size_t len) {
  size_t vo = set_impl(ofs, key.key, key.hash, len, nullptr, Type::Bytes);
  return {reinterpret_cast<std::byte *>(m_data.data() + vo + 1 +
                                        sizeof(uint32_t)),
          len};
}

// Throwing Getters
int64_t Buffer::get_i64(size_t ofs, HashedKey key) const {
  Type t = Type::Invalid;
  auto *p = get_impl(ofs, key.key, key.hash, t);
  if (!p)
    return 0;
  if (t != Type::Int64)
    throw exception("Type mismatch");
  const size_t rem = bound_size() -
                     static_cast<size_t>(reinterpret_cast<const uint8_t *>(p) - data());
  if (rem < 8)
    throw exception("Corrupt value");
  int64_t v;
  std::memcpy(&v, p, 8);
  return v;
}
double Buffer::get_f64(size_t ofs, HashedKey key) const {
  Type t = Type::Invalid;
  auto *p = get_impl(ofs, key.key, key.hash, t);
  if (!p || t != Type::Float64)
    throw exception("Type mismatch or not found");
  const size_t rem = bound_size() -
                     static_cast<size_t>(reinterpret_cast<const uint8_t *>(p) - data());
  if (rem < 8)
    throw exception("Corrupt value");
  double v;
  std::memcpy(&v, p, 8);
  return v;
}
bool Buffer::get_bool(size_t ofs, HashedKey key) const {
  Type t = Type::Invalid;
  auto *p = get_impl(ofs, key.key, key.hash, t);
  if (!p || t != Type::Bool)
    throw exception("Type mismatch or not found");
  const size_t rem = bound_size() -
                     static_cast<size_t>(reinterpret_cast<const uint8_t *>(p) - data());
  if (rem < 1)
    throw exception("Corrupt value");
  uint8_t raw = 0;
  std::memcpy(&raw, p, 1);
  return raw != 0;
}
std::string_view Buffer::get_str(size_t ofs, HashedKey key) const {
  Type t = Type::Invalid;
  auto *p = get_impl(ofs, key.key, key.hash, t);
  if (!p || t != Type::String)
    return "";
  const size_t rem = bound_size() -
                     static_cast<size_t>(reinterpret_cast<const uint8_t *>(p) - data());
  if (rem < 4)
    throw exception("Corrupt string length");
  uint32_t sz;
  std::memcpy(&sz, p, 4);
  if (sz > rem - 4)
    throw exception("Corrupt string length");
  return std::string_view(reinterpret_cast<const char *>(p + 4), sz);
}

size_t Buffer::set_obj(size_t ofs, HashedKey key) {
  size_t o = set_impl(ofs, key.key, key.hash, 0, nullptr, Type::Object);
  size_t pos = o + 1;
  ensure_capacity(config::node_size); // Ensure space for initialization
  std::memset(m_data.data() + pos, 0, config::node_size);
  MutableNodeView n(reinterpret_cast<PackedNodeLayout *>(m_data.data() + pos));
  n.set_gen_type(1, Type::Object);
  m_used_size += config::node_size;
  return pos; // Return Offset of the Node
}
size_t Buffer::set_arr(size_t ofs, HashedKey key) {
  size_t o = set_impl(ofs, key.key, key.hash, 0, nullptr, Type::Array);
  size_t pos = o + 1;
  ensure_capacity(config::node_size);
  std::memset(m_data.data() + pos, 0, config::node_size);
  MutableNodeView n(reinterpret_cast<PackedNodeLayout *>(m_data.data() + pos));
  n.set_gen_type(1, Type::Array);
  m_used_size += config::node_size;
  return pos;
}

// Array appends
size_t Buffer::arr_append_impl(size_t ofs, size_t val_len, const void *val_ptr,
                               Type type) {
  if (m_used_size < config::node_size || ofs > m_used_size - config::node_size) {
    throw exception("Invalid offset");
  }
  NodeView arr_view(reinterpret_cast<const PackedNodeLayout *>(data() + ofs));
  if (arr_view.type() != Type::Array) {
    throw exception("Not an array");
  }
  size_t current_size = arr_view.size();

  size_t vo = set_impl(ofs, {}, static_cast<uint32_t>(current_size), val_len,
                       val_ptr, type, true);

  // Re-acquire pointer as set_impl might have resized m_data
  MutableNodeView arr(
      reinterpret_cast<PackedNodeLayout *>(m_data.data() + ofs));

  // Update size (key_count was updated by set_impl)
  arr.set_size(static_cast<uint32_t>(current_size + 1));
  return vo;
}

void Buffer::arr_append_null(size_t ofs) {
  arr_append_impl(ofs, 0, nullptr, Type::Null);
}
void Buffer::arr_append_bool(size_t ofs, bool v) {
  arr_append_impl(ofs, sizeof(v), &v, Type::Bool);
}
void Buffer::arr_append_i64(size_t ofs, int64_t v) {
  arr_append_impl(ofs, sizeof(v), &v, Type::Int64);
}
void Buffer::arr_append_f64(size_t ofs, double v) {
  arr_append_impl(ofs, sizeof(v), &v, Type::Float64);
}
void Buffer::arr_append_str(size_t ofs, std::string_view v) {
  arr_append_impl(ofs, v.size(), v.data(), Type::String);
}
void Buffer::arr_append_bytes(size_t ofs, std::span<const std::byte> v) {
  arr_append_impl(ofs, v.size(), v.data(), Type::Bytes);
}
std::span<std::byte> Buffer::arr_append_bytes_uninitialized(size_t ofs,
                                                            size_t len) {
  size_t vo = arr_append_impl(ofs, len, nullptr, Type::Bytes);
  return {reinterpret_cast<std::byte *>(m_data.data() + vo + 1 +
                                        sizeof(uint32_t)),
          len};
}
size_t Buffer::arr_append_obj(size_t ofs) {
  if (m_used_size < config::node_size || ofs > m_used_size - config::node_size) {
    throw exception("Invalid offset");
  }
  NodeView arr_view(reinterpret_cast<const PackedNodeLayout *>(data() + ofs));
  if (arr_view.type() != Type::Array) {
    throw exception("Not an array");
  }
  size_t idx = arr_view.size();
  size_t o = set_impl(ofs, {}, static_cast<uint32_t>(idx), 0, nullptr,
                      Type::Object, true);
  size_t pos = o + 1;

  ensure_capacity(config::node_size);
  std::memset(m_data.data() + pos, 0, config::node_size);
  MutableNodeView n(reinterpret_cast<PackedNodeLayout *>(m_data.data() + pos));
  n.set_gen_type(1, Type::Object);
  m_used_size += config::node_size;

  // Re-acquire in case ensure_capacity resized
  MutableNodeView arr(
      reinterpret_cast<PackedNodeLayout *>(m_data.data() + ofs));
  arr.set_size(static_cast<uint32_t>(idx + 1));

  return pos;
}
size_t Buffer::arr_append_arr(size_t ofs) {
  if (m_used_size < config::node_size || ofs > m_used_size - config::node_size) {
    throw exception("Invalid offset");
  }
  NodeView arr_view(reinterpret_cast<const PackedNodeLayout *>(data() + ofs));
  if (arr_view.type() != Type::Array) {
    throw exception("Not an array");
  }
  size_t idx = arr_view.size();
  size_t o = set_impl(ofs, {}, static_cast<uint32_t>(idx), 0, nullptr,
                      Type::Array, true);
  size_t pos = o + 1;

  ensure_capacity(config::node_size);
  std::memset(m_data.data() + pos, 0, config::node_size);
  MutableNodeView n(reinterpret_cast<PackedNodeLayout *>(m_data.data() + pos));
  n.set_gen_type(1, Type::Array);
  m_used_size += config::node_size;

  MutableNodeView arr(
      reinterpret_cast<PackedNodeLayout *>(m_data.data() + ofs));
  arr.set_size(static_cast<uint32_t>(idx + 1));

  return pos;
}

// Array Getters
const std::byte *Buffer::arr_get_impl(size_t ofs, uint32_t index,
                                      Type &type) const {
  return get_impl(ofs, {}, index, type, true);
}

const std::byte *Buffer::arr_get_impl_noexcept(size_t ofs, uint32_t index,
                                               Type &type) const noexcept {
  return get_impl_noexcept(ofs, {}, index, type, true);
}

int64_t Buffer::arr_get_i64(size_t ofs, uint32_t index) const {
  Type t = Type::Invalid;
  auto *p = arr_get_impl(ofs, index, t);
  if (!p || t != Type::Int64)
    throw exception("Type mismatch");
  const size_t rem = bound_size() -
                     static_cast<size_t>(reinterpret_cast<const uint8_t *>(p) - data());
  if (rem < 8)
    throw exception("Corrupt value");
  int64_t v;
  std::memcpy(&v, p, 8);
  return v;
}
double Buffer::arr_get_f64(size_t ofs, uint32_t index) const {
  Type t = Type::Invalid;
  auto *p = arr_get_impl(ofs, index, t);
  if (!p || t != Type::Float64)
    throw exception("Type mismatch");
  const size_t rem = bound_size() -
                     static_cast<size_t>(reinterpret_cast<const uint8_t *>(p) - data());
  if (rem < 8)
    throw exception("Corrupt value");
  double v;
  std::memcpy(&v, p, 8);
  return v;
}
bool Buffer::arr_get_bool(size_t ofs, uint32_t index) const {
  Type t = Type::Invalid;
  auto *p = arr_get_impl(ofs, index, t);
  if (!p || t != Type::Bool)
    throw exception("Type mismatch");
  const size_t rem = bound_size() -
                     static_cast<size_t>(reinterpret_cast<const uint8_t *>(p) - data());
  if (rem < 1)
    throw exception("Corrupt value");
  uint8_t raw = 0;
  std::memcpy(&raw, p, 1);
  return raw != 0;
}
std::string_view Buffer::arr_get_str(size_t ofs, uint32_t index) const {
  Type t = Type::Invalid;
  auto *p = arr_get_impl(ofs, index, t);
  if (!p || t != Type::String)
    throw exception("Type mismatch");
  const size_t rem = bound_size() -
                     static_cast<size_t>(reinterpret_cast<const uint8_t *>(p) - data());
  if (rem < 4)
    throw exception("Corrupt string length");
  uint32_t sz;
  std::memcpy(&sz, p, 4);
  if (sz > rem - 4)
    throw exception("Corrupt string length");
  return std::string_view(reinterpret_cast<const char *>(p + 4), sz);
}
std::span<const std::byte> Buffer::arr_get_bytes(size_t ofs,
                                                 uint32_t index) const {
  Type t = Type::Invalid;
  auto *p = arr_get_impl(ofs, index, t);
  if (!p || t != Type::Bytes)
    throw exception("Type mismatch");
  const size_t rem = bound_size() -
                     static_cast<size_t>(reinterpret_cast<const uint8_t *>(p) - data());
  if (rem < sizeof(uint32_t))
    throw exception("Corrupt bytes length");
  uint32_t sz;
  std::memcpy(&sz, p, 4);
  if (sz > rem - sizeof(uint32_t))
    throw exception("Corrupt bytes length");
  return {reinterpret_cast<const std::byte *>(p + 4), sz};
}
size_t Buffer::arr_get_obj(size_t ofs, uint32_t index) const {
  Type t = Type::Invalid;
  auto *p = arr_get_impl(ofs, index, t);
  if (!p || t != Type::Object)
    throw exception("Type mismatch");
  const size_t node_ofs =
      static_cast<size_t>(reinterpret_cast<const uint8_t *>(p) - data());
  if (bound_size() - node_ofs < config::node_size)
    throw exception("Corrupt nested node");
  return node_ofs;
}
size_t Buffer::arr_get_arr(size_t ofs, uint32_t index) const {
  Type t = Type::Invalid;
  auto *p = arr_get_impl(ofs, index, t);
  if (!p || t != Type::Array)
    throw exception("Type mismatch");
  const size_t node_ofs =
      static_cast<size_t>(reinterpret_cast<const uint8_t *>(p) - data());
  if (bound_size() - node_ofs < config::node_size)
    throw exception("Corrupt nested node");
  return node_ofs;
}
Type Buffer::arr_get_type(size_t ofs, uint32_t index) const {
  Type t = Type::Invalid;
  arr_get_impl(ofs, index, t);
  return t;
}
Type Buffer::get_type(size_t ofs, HashedKey key) const {
  Type t = Type::Invalid;
  get_impl(ofs, key.key, key.hash, t);
  return t;
}
Type Buffer::get_type_noexcept(size_t ofs, HashedKey key) const noexcept {
  Type t = Type::Invalid;
  get_impl_noexcept(ofs, key.key, key.hash, t);
  return t;
}

size_t Buffer::get_obj(size_t ofs, HashedKey key) const {
  Type t = Type::Invalid;
  auto *p = get_impl(ofs, key.key, key.hash, t);
  if (!p || t != Type::Object)
    throw exception("Type mismatch");
  const size_t node_ofs =
      static_cast<size_t>(reinterpret_cast<const uint8_t *>(p) - data());
  if (bound_size() - node_ofs < config::node_size)
    throw exception("Corrupt nested node");
  return node_ofs;
}
size_t Buffer::get_arr(size_t ofs, HashedKey key) const {
  Type t = Type::Invalid;
  auto *p = get_impl(ofs, key.key, key.hash, t);
  if (!p || t != Type::Array)
    throw exception("Type mismatch");
  const size_t node_ofs =
      static_cast<size_t>(reinterpret_cast<const uint8_t *>(p) - data());
  if (bound_size() - node_ofs < config::node_size)
    throw exception("Corrupt nested node");
  return node_ofs;
}

std::span<const std::byte> Buffer::get_bytes(size_t ofs, HashedKey key) const {
  Type type = Type::Invalid;
  const std::byte *ptr = get_impl(ofs, key.key, key.hash, type, false);

  if (ptr && type == Type::Bytes) {
    const size_t rem = bound_size() -
                       static_cast<size_t>(reinterpret_cast<const uint8_t *>(ptr) - data());
    if (rem < sizeof(uint32_t))
      throw exception("Corrupt bytes length");
    uint32_t size;
    std::memcpy(&size, ptr, sizeof(uint32_t));
    if (size > rem - sizeof(uint32_t))
      throw exception("Corrupt bytes length");
    return {reinterpret_cast<const std::byte *>(ptr + sizeof(uint32_t)), size};
  }
  return {};
}

// Single-pass noexcept try_get_* / arr_try_get_*
namespace {

std::optional<bool> decode_bool_noexcept(const uint8_t *base, size_t bound,
                                         const std::byte *p,
                                         Type t) noexcept {
  if (!p || t != Type::Bool)
    return std::nullopt;
  const size_t rem =
      bound - static_cast<size_t>(reinterpret_cast<const uint8_t *>(p) - base);
  if (rem < 1)
    return std::nullopt;
  uint8_t raw = 0;
  std::memcpy(&raw, p, 1);
  return raw != 0;
}

std::optional<int64_t> decode_i64_noexcept(const uint8_t *base, size_t bound,
                                           const std::byte *p,
                                           Type t) noexcept {
  if (!p || t != Type::Int64)
    return std::nullopt;
  const size_t rem =
      bound - static_cast<size_t>(reinterpret_cast<const uint8_t *>(p) - base);
  if (rem < 8)
    return std::nullopt;
  int64_t v;
  std::memcpy(&v, p, 8);
  return v;
}

std::optional<double> decode_f64_noexcept(const uint8_t *base, size_t bound,
                                          const std::byte *p,
                                          Type t) noexcept {
  if (!p || t != Type::Float64)
    return std::nullopt;
  const size_t rem =
      bound - static_cast<size_t>(reinterpret_cast<const uint8_t *>(p) - base);
  if (rem < 8)
    return std::nullopt;
  double v;
  std::memcpy(&v, p, 8);
  return v;
}

std::optional<std::string_view> decode_str_noexcept(const uint8_t *base,
                                                    size_t bound,
                                                    const std::byte *p,
                                                    Type t) noexcept {
  if (!p || t != Type::String)
    return std::nullopt;
  const size_t rem =
      bound - static_cast<size_t>(reinterpret_cast<const uint8_t *>(p) - base);
  if (rem < 4)
    return std::nullopt;
  uint32_t sz;
  std::memcpy(&sz, p, 4);
  if (sz > rem - 4)
    return std::nullopt;
  return std::string_view(reinterpret_cast<const char *>(p + 4), sz);
}

std::optional<std::span<const std::byte>>
decode_bytes_noexcept(const uint8_t *base, size_t bound, const std::byte *p,
                      Type t) noexcept {
  if (!p || t != Type::Bytes)
    return std::nullopt;
  const size_t rem =
      bound - static_cast<size_t>(reinterpret_cast<const uint8_t *>(p) - base);
  if (rem < sizeof(uint32_t))
    return std::nullopt;
  uint32_t sz;
  std::memcpy(&sz, p, sizeof(uint32_t));
  if (sz > rem - sizeof(uint32_t))
    return std::nullopt;
  return std::span<const std::byte>{p + sizeof(uint32_t), sz};
}

std::optional<size_t> decode_node_ofs_noexcept(const uint8_t *base,
                                               size_t bound,
                                               const std::byte *p, Type t,
                                               Type expected) noexcept {
  if (!p || t != expected)
    return std::nullopt;
  const size_t node_ofs =
      static_cast<size_t>(reinterpret_cast<const uint8_t *>(p) - base);
  if (bound - node_ofs < config::node_size)
    return std::nullopt;
  return node_ofs;
}

} // namespace

std::optional<bool> Buffer::try_get_bool(size_t ofs,
                                         HashedKey key) const noexcept {
  Type t = Type::Invalid;
  const std::byte *p = get_impl_noexcept(ofs, key.key, key.hash, t);
  return decode_bool_noexcept(data(), bound_size(), p, t);
}
std::optional<int64_t> Buffer::try_get_i64(size_t ofs,
                                           HashedKey key) const noexcept {
  Type t = Type::Invalid;
  const std::byte *p = get_impl_noexcept(ofs, key.key, key.hash, t);
  return decode_i64_noexcept(data(), bound_size(), p, t);
}
std::optional<double> Buffer::try_get_f64(size_t ofs,
                                          HashedKey key) const noexcept {
  Type t = Type::Invalid;
  const std::byte *p = get_impl_noexcept(ofs, key.key, key.hash, t);
  return decode_f64_noexcept(data(), bound_size(), p, t);
}
std::optional<std::string_view>
Buffer::try_get_str(size_t ofs, HashedKey key) const noexcept {
  Type t = Type::Invalid;
  const std::byte *p = get_impl_noexcept(ofs, key.key, key.hash, t);
  return decode_str_noexcept(data(), bound_size(), p, t);
}
std::optional<std::span<const std::byte>>
Buffer::try_get_bytes(size_t ofs, HashedKey key) const noexcept {
  Type t = Type::Invalid;
  const std::byte *p = get_impl_noexcept(ofs, key.key, key.hash, t);
  return decode_bytes_noexcept(data(), bound_size(), p, t);
}
std::optional<size_t> Buffer::try_get_obj(size_t ofs,
                                          HashedKey key) const noexcept {
  Type t = Type::Invalid;
  const std::byte *p = get_impl_noexcept(ofs, key.key, key.hash, t);
  return decode_node_ofs_noexcept(data(), bound_size(), p, t, Type::Object);
}
std::optional<size_t> Buffer::try_get_arr(size_t ofs,
                                          HashedKey key) const noexcept {
  Type t = Type::Invalid;
  const std::byte *p = get_impl_noexcept(ofs, key.key, key.hash, t);
  return decode_node_ofs_noexcept(data(), bound_size(), p, t, Type::Array);
}

std::optional<bool> Buffer::arr_try_get_bool(size_t ofs,
                                             uint32_t index) const noexcept {
  Type t = Type::Invalid;
  const std::byte *p = arr_get_impl_noexcept(ofs, index, t);
  return decode_bool_noexcept(data(), bound_size(), p, t);
}
std::optional<int64_t> Buffer::arr_try_get_i64(size_t ofs,
                                               uint32_t index) const noexcept {
  Type t = Type::Invalid;
  const std::byte *p = arr_get_impl_noexcept(ofs, index, t);
  return decode_i64_noexcept(data(), bound_size(), p, t);
}
std::optional<double> Buffer::arr_try_get_f64(size_t ofs,
                                              uint32_t index) const noexcept {
  Type t = Type::Invalid;
  const std::byte *p = arr_get_impl_noexcept(ofs, index, t);
  return decode_f64_noexcept(data(), bound_size(), p, t);
}
std::optional<std::string_view>
Buffer::arr_try_get_str(size_t ofs, uint32_t index) const noexcept {
  Type t = Type::Invalid;
  const std::byte *p = arr_get_impl_noexcept(ofs, index, t);
  return decode_str_noexcept(data(), bound_size(), p, t);
}
std::optional<std::span<const std::byte>>
Buffer::arr_try_get_bytes(size_t ofs, uint32_t index) const noexcept {
  Type t = Type::Invalid;
  const std::byte *p = arr_get_impl_noexcept(ofs, index, t);
  return decode_bytes_noexcept(data(), bound_size(), p, t);
}
std::optional<size_t> Buffer::arr_try_get_obj(size_t ofs,
                                              uint32_t index) const noexcept {
  Type t = Type::Invalid;
  const std::byte *p = arr_get_impl_noexcept(ofs, index, t);
  return decode_node_ofs_noexcept(data(), bound_size(), p, t, Type::Object);
}
std::optional<size_t> Buffer::arr_try_get_arr(size_t ofs,
                                              uint32_t index) const noexcept {
  Type t = Type::Invalid;
  const std::byte *p = arr_get_impl_noexcept(ofs, index, t);
  return decode_node_ofs_noexcept(data(), bound_size(), p, t, Type::Array);
}

Iterator Buffer::begin(size_t ofs) const {
  if (bound_size() < sizeof(PackedNodeLayout) || data() == nullptr)
    return Iterator(nullptr, 0, 0, 0);
  NodeView root(reinterpret_cast<const PackedNodeLayout *>(data()));
  return Iterator(this, 0, ofs, root.generation());
}
Iterator Buffer::end(size_t) const { return Iterator(nullptr, 0, 0, 0); }

} // namespace lite3cpp
