#include "array.hpp"
#include "buffer.hpp"

namespace lite3cpp {

void Array::push_back(bool val) { m_buffer->arr_append_bool(m_offset, val); }

void Array::push_back(int64_t val) { m_buffer->arr_append_i64(m_offset, val); }

void Array::push_back(double val) { m_buffer->arr_append_f64(m_offset, val); }

void Array::push_back(std::string_view val) {
  m_buffer->arr_append_str(m_offset, val);
}

size_t Array::size() const {
  return 0; // Placeholder
}

} // namespace lite3cpp
