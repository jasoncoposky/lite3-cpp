#include "document.hpp"
#include <utility>

namespace lite3cpp {

Document::Document() : m_buffer() { m_buffer.init_object(); }

Document::Document(size_t initial_size) : m_buffer(initial_size) {
  m_buffer.init_object();
}

Document::Document(Buffer buf) : m_buffer(std::move(buf)) {}

Object Document::root_obj() { return Object(&m_buffer, 0); }

Array Document::root_arr() { return Array(&m_buffer, 0); }

} // namespace lite3cpp
