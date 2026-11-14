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
  if (m_buffer && m_buffer->bound_size() >= sizeof(PackedNodeLayout) &&
      node_offset <= m_buffer->bound_size() - sizeof(PackedNodeLayout)) {
    m_depth = 0;
    m_stack[m_depth] = {node_offset, 0};
    find_first();
    if (m_depth >= 0) {
      find_next();
    }
  } else {
    m_buffer = nullptr;
  }
}

Iterator &Iterator::operator++() {
  find_next();
  return *this;
}

const Iterator::value_type &Iterator::operator*() const {
  if (!m_buffer || m_buffer->bound_size() < sizeof(PackedNodeLayout))
    throw lite3cpp::exception("Invalid iterator");
  NodeView root_node(
      reinterpret_cast<const PackedNodeLayout *>(m_buffer->data()));
  if (m_initial_buffer_generation != root_node.generation()) {
    throw lite3cpp::exception(
        "Iterator invalidated: Buffer modified during iteration.");
  }
  return m_current_value;
}

const Iterator::value_type *Iterator::operator->() const {
  if (!m_buffer || m_buffer->bound_size() < sizeof(PackedNodeLayout))
    throw lite3cpp::exception("Invalid iterator");
  NodeView root_node(
      reinterpret_cast<const PackedNodeLayout *>(m_buffer->data()));
  if (m_initial_buffer_generation != root_node.generation()) {
    throw lite3cpp::exception(
        "Iterator invalidated: Buffer modified during iteration.");
  }
  return &m_current_value;
}

std::string_view Iterator::key() const { return (**this).key; }

bool Iterator::operator==(const Iterator &other) const {
  if (!m_buffer && !other.m_buffer)
    return true;
  if (!m_buffer || !other.m_buffer)
    return false;

  if (m_buffer->bound_size() < sizeof(PackedNodeLayout))
    return false;
  NodeView root_node(
      reinterpret_cast<const PackedNodeLayout *>(m_buffer->data()));
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
  if (m_buffer->bound_size() < sizeof(PackedNodeLayout))
    return;
  NodeView root_node(
      reinterpret_cast<const PackedNodeLayout *>(m_buffer->data()));
  if (m_initial_buffer_generation != root_node.generation()) {
    m_buffer = nullptr;
    return;
  }

  NodeView current_node(reinterpret_cast<const PackedNodeLayout *>(
      m_buffer->data() + m_stack[m_depth].offset));

  while (current_node.get_child_offset(0) != 0) {
    if (m_depth >= static_cast<int>(config::tree_height_max)) {
      throw lite3cpp::exception("Corrupt B-tree: max height exceeded");
    }
    size_t child_ofs = current_node.get_child_offset(0);
    if (child_ofs > m_buffer->bound_size() - sizeof(PackedNodeLayout)) {
      m_buffer = nullptr;
      return;
    }
    m_depth++;
    m_stack[m_depth] = {child_ofs, 0};
    // Re-acquire pointer for next level
    current_node = NodeView(reinterpret_cast<const PackedNodeLayout *>(
        m_buffer->data() + m_stack[m_depth].offset));
  }
}

void Iterator::find_next() {
  lite3cpp::log_if_enabled(lite3cpp::LogLevel::Debug,
                           "Iterator::find_next called.", "IteratorNext",
                           std::chrono::microseconds(0), 0);

  if (m_buffer) {
    if (m_buffer->bound_size() < sizeof(PackedNodeLayout)) {
      m_buffer = nullptr;
      return;
    }
    NodeView root_node(
        reinterpret_cast<const PackedNodeLayout *>(m_buffer->data()));
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
      m_buffer->data() + m_stack[m_depth].offset));

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
  if (!decode_key_tag(m_buffer->data(), m_buffer->bound_size(),
                      kv_offset, key_size, tag_size)) {
    m_buffer = nullptr;
    return;
  }

  m_current_value.key = std::string_view(
      reinterpret_cast<const char *>(m_buffer->data() + kv_offset +
                                     tag_size),
      key_size - 1);
  m_current_value.value_offset = kv_offset + tag_size + key_size;

  if (m_current_value.value_offset >= m_buffer->bound_size()) {
    m_buffer = nullptr;
    return;
  }
  m_current_value.value_type =
      static_cast<Type>(m_buffer->data()[m_current_value.value_offset]);

  m_stack[m_depth].key_index++;
  if (current_node.get_child_offset(m_stack[m_depth].key_index) != 0) {
    if (m_depth >= static_cast<int>(config::tree_height_max)) {
      throw lite3cpp::exception("Corrupt B-tree: max height exceeded");
    }
    size_t child_ofs =
        current_node.get_child_offset(m_stack[m_depth].key_index);
    if (child_ofs > m_buffer->bound_size() - sizeof(PackedNodeLayout)) {
      m_buffer = nullptr;
      return;
    }
    m_depth++;
    m_stack[m_depth] = {child_ofs, 0};
    find_first();
  }
}

} // namespace lite3cpp
