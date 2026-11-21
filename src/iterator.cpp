#include "iterator.hpp"
#include "buffer.hpp"
#include "exception.hpp"
#include "observability.hpp"
#include <chrono>
#include <cstring>

namespace lite3cpp {

namespace {
#if defined(__GNUC__) || defined(__clang__)
[[noreturn]] __attribute__((noinline, cold))
#else
[[noreturn]]
#endif
void throw_iter_error(const char *msg) {
  throw lite3cpp::exception(msg);
}
} // namespace

Iterator::Iterator(const Buffer *buffer, size_t /*ofs*/, size_t node_offset,
                   uint32_t initial_buffer_generation)
    : m_buffer(buffer),
      m_initial_data(buffer ? buffer->data() : nullptr),
      m_initial_bound_size(buffer ? buffer->bound_size() : 0),
      m_initial_buffer_generation(initial_buffer_generation),
      m_initial_node_generation(0),
      m_depth(-1),
      m_is_array(false) {
  if (m_buffer && m_buffer->bound_size() >= sizeof(PackedNodeLayout) &&
      node_offset <= m_buffer->bound_size() - sizeof(PackedNodeLayout)) {
    NodeView start_node(reinterpret_cast<const PackedNodeLayout *>(
        m_buffer->data() + node_offset));
    if (start_node.type() != Type::Object &&
        start_node.type() != Type::Array) {
      m_buffer = nullptr;
      return;
    }
    m_initial_node_generation = start_node.generation();
    m_is_array = (start_node.type() == Type::Array);
    m_depth = 0;
    m_stack[m_depth] = {node_offset, 0};
    find_first();
    if (m_depth >= 0 && m_buffer != nullptr) {
      find_next();
    }
  } else {
    m_buffer = nullptr;
  }
}

Iterator &Iterator::operator++() {
  if (m_buffer) [[likely]] {
    if (is_modified()) [[unlikely]] {
      throw_iter_error(
          "Iterator invalidated: Buffer modified during iteration.");
    }
    find_next();
  }
  return *this;
}

bool Iterator::equals_non_end(const Iterator &other) const {
  if (is_modified() || other.is_modified()) {
    return false;
  }

  return m_depth == other.m_depth &&
         m_stack[m_depth].offset == other.m_stack[other.m_depth].offset &&
         m_stack[m_depth].key_index == other.m_stack[other.m_depth].key_index;
}

void Iterator::find_first() {
  if (!m_buffer) [[unlikely]]
    return;
  const uint8_t *const base = m_initial_data;
  const size_t max_node_ofs = m_initial_bound_size - sizeof(PackedNodeLayout);

  if (m_depth < 0 || m_stack[m_depth].offset > max_node_ofs) [[unlikely]] {
    m_buffer = nullptr;
    return;
  }

  const auto *packed =
      reinterpret_cast<const PackedNodeLayout *>(base + m_stack[m_depth].offset);

  while (packed->child_ofs[0] != 0) {
    if (m_depth >= static_cast<int>(config::tree_height_max)) [[unlikely]] {
      throw lite3cpp::exception("Corrupt B-tree: max height exceeded");
    }
    const size_t child_ofs = packed->child_ofs[0];
    if (child_ofs > max_node_ofs) [[unlikely]] {
      m_buffer = nullptr;
      return;
    }
    m_depth++;
    m_stack[m_depth] = {child_ofs, 0};
    packed = reinterpret_cast<const PackedNodeLayout *>(base + child_ofs);
  }
}

void Iterator::find_next() {
  lite3cpp::log_if_enabled(lite3cpp::LogLevel::Debug,
                           "Iterator::find_next called.", "IteratorNext",
                           std::chrono::microseconds(0), 0);

  const uint8_t *const base = m_initial_data;
  const size_t bound = m_initial_bound_size;
  const size_t max_node_ofs = bound - sizeof(PackedNodeLayout);

  while (m_depth >= 0) {
    const size_t cur_ofs = m_stack[m_depth].offset;
    if (cur_ofs > max_node_ofs) [[unlikely]] {
      m_buffer = nullptr;
      return;
    }

    const auto *packed =
        reinterpret_cast<const PackedNodeLayout *>(base + cur_ofs);
    const int kc = static_cast<int>((packed->size_kc & NODE_KEY_COUNT_MASK) >>
                                    NODE_KEY_COUNT_SHIFT);
    const int kidx = m_stack[m_depth].key_index;

    if (kidx >= kc) {
      m_depth--;
      continue;
    }

    const size_t kv_offset = packed->kv_ofs[kidx];

    if (m_is_array) {
      m_current_value.key = {};
      m_current_value.value_offset = kv_offset;
    } else {
      uint32_t key_size = 0;
      size_t tag_size = 0;
      if (!decode_key_tag(base, bound, kv_offset, key_size, tag_size)) [[unlikely]] {
        m_buffer = nullptr;
        return;
      }
      m_current_value.key = std::string_view(
          reinterpret_cast<const char *>(base + kv_offset + tag_size),
          key_size - 1);
      m_current_value.value_offset = kv_offset + tag_size + key_size;
    }

    if (m_current_value.value_offset >= bound) [[unlikely]] {
      m_buffer = nullptr;
      return;
    }
    m_current_value.value_type =
        static_cast<Type>(base[m_current_value.value_offset]);

    const int next_idx = kidx + 1;
    m_stack[m_depth].key_index = next_idx;
    size_t child_ofs = packed->child_ofs[next_idx];
    if (child_ofs != 0) [[unlikely]] {
      while (true) {
        if (m_depth >= static_cast<int>(config::tree_height_max)) [[unlikely]] {
          throw_iter_error("Corrupt B-tree: max height exceeded");
        }
        if (child_ofs > max_node_ofs) [[unlikely]] {
          m_buffer = nullptr;
          return;
        }
        m_depth++;
        m_stack[m_depth] = {child_ofs, 0};
        const auto *child_packed =
            reinterpret_cast<const PackedNodeLayout *>(base + child_ofs);
        child_ofs = child_packed->child_ofs[0];
        if (child_ofs == 0) {
          break;
        }
      }
    } else if (next_idx >= kc) {
      // Pop exhausted leaf/internal nodes eagerly to avoid re-reading on next step
      m_depth--;
    }
    return;
  }

  m_buffer = nullptr;
}

} // namespace lite3cpp
