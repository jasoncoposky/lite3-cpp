#ifndef LITE3CPP_VALUE_HPP
#define LITE3CPP_VALUE_HPP

#include "node.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace lite3cpp {

class Buffer;

class Value {
public:
  Value(Buffer *buf, size_t parent_ofs);
  virtual ~Value() = default;

  Type type() const;

  // Indexing
  Value operator[](std::string_view key);
  Value operator[](const char *key) { return (*this)[std::string_view(key)]; }
  Value operator[](uint32_t index);
  Value operator[](int index) { return (*this)[static_cast<uint32_t>(index)]; }

  // Explicit Casting
  explicit operator bool() const;
  explicit operator int64_t() const;
  explicit operator double() const;
  explicit operator std::string_view() const;
  explicit operator std::span<const std::byte>() const;

  // Direct Comparisons to avoid GTest/Ambiguity issues
  bool operator==(bool other) const {
    return static_cast<bool>(*this) == other;
  }
  bool operator==(int64_t other) const {
    return static_cast<int64_t>(*this) == other;
  }
  bool operator==(double other) const {
    return static_cast<double>(*this) == other;
  }
  bool operator==(std::string_view other) const {
    return static_cast<std::string_view>(*this) == other;
  }
  bool operator==(const char *other) const {
    return *this == std::string_view(other);
  }

  // Assignment
  Value &operator=(bool val);
  Value &operator=(int64_t val);
  Value &operator=(double val);
  Value &operator=(std::string_view val);
  Value &operator=(const char *val);
  Value &operator=(std::span<const std::byte> val);

protected:
  Buffer *m_buffer;
  size_t m_offset;
  size_t m_parent_ofs;
  std::string m_key;
  uint32_t m_index;
  bool m_is_array_element;

  friend class Object;
  friend class Array;
};

} // namespace lite3cpp

#endif // LITE3CPP_VALUE_HPP
