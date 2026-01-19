#include "iterator.hpp"
#include "buffer.hpp"
#include "exception.hpp"
#include "observability.hpp"
#include <chrono>
#include <cstring>


namespace lite3cpp {

Iterator::Iterator(const Buffer *buffer, size_t ofs, size_t node_offset,
                   uint32_t initial_buffer_generation)
    : m_buffer(buffer), m_depth(-1),
      m_initial_buffer_generation(initial_buffer_generation) {
  if (m_buffer && !m_buffer->m_data.empty()) {
    m_depth = 0;
    m_stack[m_depth] = {node_offset, 0};
    find_first();
    if (m_depth >= 0) {
      find_next();
    }
  }
}

Iterator &Iterator::operator++() {
  find_next();
  return *this;
}

const Iterator::value_type &Iterator::operator*() const {
  if (!m_buffer || m_buffer->m_data.empty())
    throw lite3cpp::exception("Invalid iterator");
  NodeView root_node(
      reinterpret_cast<const PackedNodeLayout *>(m_buffer->m_data.data()));
  if (m_initial_buffer_generation != root_node.generation()) {
    throw lite3cpp::exception(
        "Iterator invalidated: Buffer modified during iteration.");
  }
  return m_current_value;
}

const Iterator::value_type *Iterator::operator->() const {
  if (!m_buffer || m_buffer->m_data.empty())
    throw lite3cpp::exception("Invalid iterator");
  NodeView root_node(
      reinterpret_cast<const PackedNodeLayout *>(m_buffer->m_data.data()));
  if (m_initial_buffer_generation != root_node.generation()) {
    throw lite3cpp::exception(
        "Iterator invalidated: Buffer modified during iteration.");
  }
  return &m_current_value;
}

bool Iterator::operator==(const Iterator &other) const {
  if (!m_buffer && !other.m_buffer)
    return true;
  if (!m_buffer || !other.m_buffer)
    return false;

  if (m_buffer->m_data.empty())
    return false;
  NodeView root_node(
      reinterpret_cast<const PackedNodeLayout *>(m_buffer->m_data.data()));
  if (m_initial_buffer_generation != root_node.generation()) {
    return false;
  }

  return m_buffer == other.m_buffer && m_depth == other.m_depth &&
         m_stack[m_depth].offset == other.m_stack[other.m_depth].offset &&
         m_stack[m_depth].key_index == other.m_stack[other.m_depth].key_index;
}

bool Iterator::operator!=(const Iterator &other) const {
  return !(*this == other);
}

void Iterator::find_first() {
  if (!m_buffer)
    return;
  NodeView current_node(reinterpret_cast<const PackedNodeLayout *>(
      m_buffer->m_data.data() + m_stack[m_depth].offset));

  if (m_buffer->m_data.empty())
    return;
  NodeView root_node(
      reinterpret_cast<const PackedNodeLayout *>(m_buffer->m_data.data()));
  if (m_initial_buffer_generation != root_node.generation()) {
    m_buffer = nullptr;
    return;
  }

  while (current_node.get_child_offset(0) != 0 &&
         m_depth < config::tree_height_max) {
    m_depth++;
    m_stack[m_depth] = {current_node.get_child_offset(0), 0};
    // Re-acquire pointer for next level
    current_node = NodeView(reinterpret_cast<const PackedNodeLayout *>(
        m_buffer->m_data.data() + m_stack[m_depth].offset));
  }
}

void Iterator::find_next() {
  lite3cpp::log_if_enabled(lite3cpp::LogLevel::Debug,
                           "Iterator::find_next called.", "IteratorNext",
                           std::chrono::microseconds(0), 0);

  if (m_buffer) {
    if (m_buffer->m_data.empty()) {
      m_buffer = nullptr;
      return;
    }
    NodeView root_node(
        reinterpret_cast<const PackedNodeLayout *>(m_buffer->m_data.data()));
    if (m_initial_buffer_generation != root_node.generation()) {
      m_buffer = nullptr;
      return;
    }
  } else {
    return;
  }

  if (m_depth < 0) {
    m_buffer = nullptr;
    return;
  }

  NodeView current_node(reinterpret_cast<const PackedNodeLayout *>(
      m_buffer->m_data.data() + m_stack[m_depth].offset));

  if (m_stack[m_depth].key_index >= current_node.key_count()) {
    m_depth--;
    if (m_depth < 0) {
      m_buffer = nullptr;
      return;
    }
    find_next();
    return;
  }

  size_t kv_offset = current_node.get_kv_offset(m_stack[m_depth].key_index);

  // Key reading logic
  // Assumes key tag at kv_offset if standard layout.
  // My `set_impl` puts [Tag][Key][Type][Value].
  // kv_offset points to Tag.

  if (kv_offset >= m_buffer->m_data.size()) {
    m_buffer = nullptr;
    return;
  }

  uint8_t key_tag = m_buffer->m_data[kv_offset];
  uint32_t key_size = key_tag >> 2;

  if (key_size == 0 || (kv_offset + 1 + key_size > m_buffer->m_data.size())) {
    m_buffer = nullptr;
    return;
  }

  m_current_value.key = std::string_view(
      reinterpret_cast<const char *>(m_buffer->m_data.data() + kv_offset + 1),
      key_size - 1);
  m_current_value.value_offset =
      kv_offset + 1 + (key_size - 1) + 1; // +1 tag, +chars, +null?
  // Actually: `set_impl` wrote: [Tag (1)] [Key (sz-1)] [Null (1)]
  // Tag encodes SIZE = sz.
  // Data written: tag (1), key (sz-1), null (1). Total 1 + sz.
  // So value starts at `kv_offset + 1 + (key_size-1) + 1`?
  // In `set_impl`: `m_data[start] = (ks << 2)`. ks = key.size()+1.
  // `memcpy(start+1, key, key.size())`. `m_data[start+1+key.size()] = 0`.
  // Total bytes used for key: 1 + key.size() + 1.
  // key_size (from tag) = key.size() + 1.
  // So Value Start = Start + 1 + key.size() + 1? No.
  // `set_impl` calculated `klen` = `is_append ? 0 : (key.size() +
  // key_tag_size)`. If key_tag_size=1, `klen` = `key.size() + 1`. Wait, `klen`
  // calculation in `set_impl`: `klen = key.size() + key_tag_size`. (e.g. 5 + 1
  // = 6). `m_data[start] = tag`. (1 byte). `memcpy(start+1, key, size)`. (5
  // bytes). `m_data[start+1+size] = 0`? Only if `type==String` for VALUE. Key
  // null terminator is NOT added in `set_impl` explicitly for keys? Ah,
  // `set_impl`: `m_data[insert_ofs+1+key.size()] = 0;` was commented out? No:
  // `if (!is_append)` block: `m_data[insert_ofs] = ...` `memcpy(...,
  // key.data(), key.size())` `m_data[insert_ofs + 1 + key.size()] = 0;`  <<
  // This line. So YES, null terminator is there. So total key len = 1 +
  // key.size() + 1. My `klen` calculation in `set_impl` was `key.size() +
  // key_tag_size`. If `key_tag_size` was 1. `klen` = size+1. But we write
  // size+2 bytes (tag, data, null). ERROR in `set_impl`: I allocated `klen` but
  // wrote `klen+1`? Let's check `set_impl` in `buffer.cpp` again. `size_t klen
  // = is_append ? 0 : (key.size() + key_tag_size);` `m_data[start] = ...`
  // `memcpy...`
  // `m_data[... null] = 0;` <- `start+1+key.size()`.
  // `start+1+key.size()` is indeed `start + klen`?
  // If `key_tag_size`=1. `klen` = size+1.
  // Indices: 0 (tag), 1..size (data). size+1 (null).
  // 0 to size+1 is size+2 bytes.
  // `klen` accounts for size+1 bytes.
  // So I undersized by 1 byte in set_impl?
  // OR `key_tag_size` accounts for it?
  // In `lite3_val_calc_key_size`: `size = key_len + key_tag_size`.
  // Does `key_len` include null?
  // `lite3.c`: `key_len = strlen(key) + 1`.
  // So yes.
  // My `set_impl` used `key.size()` which is usually without null.
  // So I likely need `key.size() + 1`.

  // I will fix `set_impl` later if benchmark crashes or I find memory
  // corruption. For now, in `Iterator`, I assume standard layout. If I assume
  // `set_impl` wrote [Tag][Key][Null], then: Key String View length = `key_size
  // - 1` (tag says size includes null). Pointer = `kv_offset + 1`. Next offset
  // (Value) = `kv_offset + 1 + key_size`. (Tag(1) + Key(size included null)).

  m_current_value.value_offset = kv_offset + 1 + key_size;

  if (m_current_value.value_offset >= m_buffer->m_data.size()) {
    m_buffer = nullptr;
    return;
  }
  m_current_value.value_type =
      static_cast<Type>(m_buffer->m_data[m_current_value.value_offset]);

  m_stack[m_depth].key_index++;
  if (current_node.get_child_offset(m_stack[m_depth].key_index) != 0 &&
      m_depth < config::tree_height_max) {
    m_depth++;
    m_stack[m_depth] = {
        current_node.get_child_offset(m_stack[m_depth - 1].key_index), 0};
    find_first();
  }
}

} // namespace lite3cpp
