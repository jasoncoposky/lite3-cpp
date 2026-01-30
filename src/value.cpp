#include "value.hpp"
#include "array.hpp"
#include "buffer.hpp"
#include "exception.hpp"
#include "object.hpp"
#include <cstdio>

namespace lite3cpp {

Value::Value(Buffer *buf, size_t parent_ofs)
    : m_buffer(buf), m_offset(0), m_parent_ofs(parent_ofs), m_index(0),
      m_is_array_element(false) {
  // printf("DEBUG: Value::Value constructor (buf=%p, pofs=%zu)\n",
  // (void*)m_buffer, m_parent_ofs);
}

Type Value::type() const {
  try {
    if (!m_key.empty())
      return m_buffer->get_type(m_parent_ofs, m_key);
    if (m_is_array_element)
      return m_buffer->arr_get_type(m_parent_ofs, m_index);
  } catch (...) {
  }
  return Type::Null;
}

Value Value::operator[](std::string_view key) {
  size_t current_node_ofs = m_offset;
  if (m_offset == 0) {
    if (!m_key.empty()) {
      try {
        current_node_ofs = m_buffer->get_obj(m_parent_ofs, m_key);
      } catch (...) {
        current_node_ofs = m_buffer->set_obj(m_parent_ofs, m_key);
      }
    } else if (m_is_array_element) {
      try {
        current_node_ofs = m_buffer->arr_get_obj(m_parent_ofs, m_index);
      } catch (...) {
        current_node_ofs = m_buffer->arr_append_obj(m_parent_ofs);
      }
    } else {
      current_node_ofs = 0;
    }
  }

  Value v(m_buffer, 0);
  v.m_parent_ofs = current_node_ofs;
  v.m_key = std::string(key);
  v.m_is_array_element = false;
  // printf("DEBUG: Value::operator[] (key=%s, cur_node_ofs=%zu, v.pofs=%zu)\n",
  // std::string(key).c_str(), current_node_ofs, v.m_parent_ofs);
  return v;
}

Value Value::operator[](uint32_t index) {
  size_t current_node_ofs = m_offset;
  if (m_offset == 0) {
    if (!m_key.empty()) {
      try {
        current_node_ofs = m_buffer->get_arr(m_parent_ofs, m_key);
      } catch (...) {
        current_node_ofs = m_buffer->set_arr(m_parent_ofs, m_key);
      }
    } else if (m_is_array_element) {
      try {
        current_node_ofs = m_buffer->arr_get_arr(m_parent_ofs, m_index);
      } catch (...) {
        current_node_ofs = m_buffer->arr_append_arr(m_parent_ofs);
      }
    }
  }

  Value v(m_buffer, 0);
  v.m_parent_ofs = current_node_ofs;
  v.m_index = index;
  v.m_is_array_element = true;
  return v;
}

Value::operator bool() const {
  try {
    if (m_is_array_element)
      return m_buffer->arr_get_bool(m_parent_ofs, m_index);
    if (!m_key.empty())
      return m_buffer->get_bool(m_parent_ofs, m_key);
  } catch (...) {
  }
  return false;
}

Value::operator int64_t() const {
  try {
    if (m_is_array_element)
      return m_buffer->arr_get_i64(m_parent_ofs, m_index);
    if (!m_key.empty())
      return m_buffer->get_i64(m_parent_ofs, m_key);
  } catch (...) {
  }
  return 0;
}

Value::operator double() const {
  try {
    if (m_is_array_element)
      return m_buffer->arr_get_f64(m_parent_ofs, m_index);
    if (!m_key.empty())
      return m_buffer->get_f64(m_parent_ofs, m_key);
  } catch (...) {
  }
  return 0.0;
}

Value::operator std::string_view() const {
  try {
    if (m_is_array_element)
      return m_buffer->arr_get_str(m_parent_ofs, m_index);
    if (!m_key.empty()) {
      std::string_view s = m_buffer->get_str(m_parent_ofs, m_key);
      return s;
    }
  } catch (const exception &e) {
    // printf("DEBUG: Value::operator std::string_view exception: %s (key=%s,
    // pofs=%zu)\n", e.what(), m_key.c_str(), m_parent_ofs);
  }
  return "";
}

Value::operator std::span<const std::byte>() const {
  try {
    if (m_is_array_element)
      return m_buffer->arr_get_bytes(m_parent_ofs, m_index);
    if (!m_key.empty())
      return m_buffer->get_bytes(m_parent_ofs, m_key);
  } catch (...) {
  }
  return {};
}

Value &Value::operator=(bool val) {
  if (!m_key.empty())
    m_buffer->set_bool(m_parent_ofs, m_key, val);
  else if (m_is_array_element)
    m_buffer->arr_append_bool(m_parent_ofs, val);
  return *this;
}

Value &Value::operator=(int64_t val) {
  if (!m_key.empty())
    m_buffer->set_i64(m_parent_ofs, m_key, val);
  else if (m_is_array_element)
    m_buffer->arr_append_i64(m_parent_ofs, val);
  return *this;
}

Value &Value::operator=(double val) {
  if (!m_key.empty())
    m_buffer->set_f64(m_parent_ofs, m_key, val);
  else if (m_is_array_element)
    m_buffer->arr_append_f64(m_parent_ofs, val);
  return *this;
}

Value &Value::operator=(std::string_view val) {
  if (!m_key.empty())
    m_buffer->set_str(m_parent_ofs, m_key, val);
  else if (m_is_array_element)
    m_buffer->arr_append_str(m_parent_ofs, val);
  return *this;
}

Value &Value::operator=(const char *val) {
  return (*this) = std::string_view(val);
}

} // namespace lite3cpp