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

  uint32_t key_size = 0;
  size_t tag_size = 0;
  if (!decode_key_tag(m_buffer->m_data.data(), m_buffer->m_data.size(),
                      kv_offset, key_size, tag_size)) {
    m_buffer = nullptr;
    return;
  }

  m_current_value.key = std::string_view(
      reinterpret_cast<const char *>(m_buffer->m_data.data() + kv_offset +
                                     tag_size),
      key_size - 1);
  m_current_value.value_offset = kv_offset + tag_size + key_size;

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
