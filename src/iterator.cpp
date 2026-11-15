#include "iterator.hpp"
#include "buffer.hpp"
#include "exception.hpp"
#include "observability.hpp"
#include <chrono>
#include <cstring>

namespace lite3cpp {

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

bool Iterator::is_modified() const noexcept {
  if (m_buffer->data() != m_initial_data ||
      m_buffer->bound_size() != m_initial_bound_size) {
    return true;
  }
  NodeView root_node(
      reinterpret_cast<const PackedNodeLayout *>(m_buffer->data()));
  if (m_initial_buffer_generation != root_node.generation()) {
    return true;
  }
  if (m_depth >= 0 && m_stack[0].offset != 0) {
    if (m_stack[0].offset > m_buffer->bound_size() - sizeof(PackedNodeLayout)) {
      return true;
    }
    NodeView start_node(reinterpret_cast<const PackedNodeLayout *>(
        m_buffer->data() + m_stack[0].offset));
    if (m_initial_node_generation != start_node.generation()) {
      return true;
    }
  }
  return false;
}

Iterator &Iterator::operator++() {
  if (m_buffer) {
    if (m_buffer->bound_size() < sizeof(PackedNodeLayout)) {
      throw lite3cpp::exception("Invalid iterator");
    }
    if (is_modified()) {
      throw lite3cpp::exception(
          "Iterator invalidated: Buffer modified during iteration.");
    }
  }
  find_next();
  return *this;
}

const Iterator::value_type &Iterator::operator*() const {
  if (!m_buffer || m_buffer->bound_size() < sizeof(PackedNodeLayout))
    throw lite3cpp::exception("Invalid iterator");
  if (is_modified()) {
    throw lite3cpp::exception(
        "Iterator invalidated: Buffer modified during iteration.");
  }
  return m_current_value;
}

const Iterator::value_type *Iterator::operator->() const {
  if (!m_buffer || m_buffer->bound_size() < sizeof(PackedNodeLayout))
    throw lite3cpp::exception("Invalid iterator");
  if (is_modified()) {
    throw lite3cpp::exception(
        "Iterator invalidated: Buffer modified during iteration.");
  }
  return &m_current_value;
}

std::string_view Iterator::key() const { return (**this).key; }

bool Iterator::operator==(const Iterator &other) const {
  if (!m_buffer && !other.m_buffer)
    return true;
  if (m_buffer != other.m_buffer)
    return false;

  if (m_buffer->bound_size() < sizeof(PackedNodeLayout) || is_modified() ||
      other.is_modified()) {
    return false;
  }

  return m_depth == other.m_depth &&
         m_stack[m_depth].offset == other.m_stack[other.m_depth].offset &&
         m_stack[m_depth].key_index == other.m_stack[other.m_depth].key_index;
}

bool Iterator::operator!=(const Iterator &other) const {
  return !(*this == other);
}

void Iterator::find_first() {
  if (!m_buffer)
    return;
  if (m_buffer->bound_size() < sizeof(PackedNodeLayout) || is_modified()) {
    m_buffer = nullptr;
    return;
  }

  if (m_depth < 0 ||
      m_stack[m_depth].offset >
          m_buffer->bound_size() - sizeof(PackedNodeLayout)) {
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
    current_node = NodeView(reinterpret_cast<const PackedNodeLayout *>(
        m_buffer->data() + m_stack[m_depth].offset));
  }
}

void Iterator::find_next() {
  lite3cpp::log_if_enabled(lite3cpp::LogLevel::Debug,
                           "Iterator::find_next called.", "IteratorNext",
                           std::chrono::microseconds(0), 0);

  if (!m_buffer)
    return;
  if (m_buffer->bound_size() < sizeof(PackedNodeLayout) || is_modified()) {
    m_buffer = nullptr;
    return;
  }

  while (m_depth >= 0) {
    if (m_stack[m_depth].offset >
        m_buffer->bound_size() - sizeof(PackedNodeLayout)) {
      m_buffer = nullptr;
      return;
    }

    NodeView current_node(reinterpret_cast<const PackedNodeLayout *>(
        m_buffer->data() + m_stack[m_depth].offset));

    if (m_stack[m_depth].key_index >=
        static_cast<int>(current_node.key_count())) {
      m_depth--;
      continue;
    }

    size_t kv_offset = current_node.get_kv_offset(m_stack[m_depth].key_index);

    if (m_is_array) {
      m_current_value.key = {};
      m_current_value.value_offset = kv_offset;
    } else {
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
    }

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
    return;
  }

  m_buffer = nullptr;
}

} // namespace lite3cpp
