#ifndef LITE3CPP_BUFFER_HPP
#define LITE3CPP_BUFFER_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "config.hpp"
#include "iterator.hpp"
#include "node.hpp"
#include "utils/hash.hpp"

namespace lite3cpp {

class Buffer {
public:
  Buffer() noexcept = default;
  explicit Buffer(size_t initial_size);
  explicit Buffer(std::vector<uint8_t> data);
  Buffer(const uint8_t* ptr, size_t len);
  explicit Buffer(std::string_view sv);

  Buffer(const Buffer&) = default;
  Buffer& operator=(const Buffer&) = default;
  Buffer(Buffer&& other) noexcept;
  Buffer& operator=(Buffer&& other) noexcept;
  ~Buffer() = default;

  static Buffer from_view(const uint8_t* ptr, size_t len) noexcept {
    Buffer b;
    if (ptr != nullptr && len > 0) {
      b.m_ext_data = ptr;
      b.m_used_size = len;
    }
    return b;
  }
  static Buffer from_view(std::span<const std::byte> bytes) noexcept {
    return from_view(reinterpret_cast<const uint8_t*>(bytes.data()),
                     bytes.size());
  }
  bool is_view() const noexcept { return m_ext_data != nullptr; }

  void init_object();
  void init_array();

  void set_null(size_t ofs, std::string_view key);
  void set_bool(size_t ofs, std::string_view key, bool value);
  void set_i64(size_t ofs, std::string_view key, int64_t value);
  void set_f64(size_t ofs, std::string_view key, double value);
  void set_str(size_t ofs, std::string_view key, std::string_view value);
  void set_bytes(size_t ofs, std::string_view key,
                 std::span<const std::byte> value);
  size_t set_obj(size_t ofs, std::string_view key);
  size_t set_arr(size_t ofs, std::string_view key);

  void arr_append_null(size_t ofs);
  void arr_append_bool(size_t ofs, bool value);
  void arr_append_i64(size_t ofs, int64_t value);
  void arr_append_f64(size_t ofs, double value);
  void arr_append_str(size_t ofs, std::string_view value);
  void arr_append_bytes(size_t ofs, std::span<const std::byte> value);
  size_t arr_append_obj(size_t ofs);
  size_t arr_append_arr(size_t ofs);

  bool get_bool(size_t ofs, std::string_view key) const;
  int64_t get_i64(size_t ofs, std::string_view key) const;
  double get_f64(size_t ofs, std::string_view key) const;
  std::string_view get_str(size_t ofs, std::string_view key) const;
  std::span<const std::byte> get_bytes(size_t ofs, std::string_view key) const;
  size_t get_obj(size_t ofs, std::string_view key) const;
  size_t get_arr(size_t ofs, std::string_view key) const;

  bool arr_get_bool(size_t ofs, uint32_t index) const;
  int64_t arr_get_i64(size_t ofs, uint32_t index) const;
  double arr_get_f64(size_t ofs, uint32_t index) const;
  std::string_view arr_get_str(size_t ofs, uint32_t index) const;
  std::span<const std::byte> arr_get_bytes(size_t ofs, uint32_t index) const;
  size_t arr_get_obj(size_t ofs, uint32_t index) const;
  size_t arr_get_arr(size_t ofs, uint32_t index) const;
  Type arr_get_type(size_t ofs, uint32_t index) const;
  Type get_type(size_t ofs, std::string_view key) const;

  // Access to raw data (read-only)
  const uint8_t *data() const noexcept {
    return m_ext_data ? m_ext_data : m_data.data();
  }
  size_t size() const noexcept { return m_used_size; }
  void reserve(size_t capacity) {
    if (m_ext_data != nullptr) {
      m_data.reserve(capacity > m_used_size ? capacity : m_used_size);
      materialize_if_view();
    } else {
      m_data.reserve(capacity);
    }
  }
  size_t capacity() const noexcept {
    return m_ext_data ? 0 : m_data.capacity();
  }
  void ensure_capacity(size_t required_bytes);
  void shrink_to_fit() {
    materialize_if_view();
    if (m_data.size() > m_used_size) {
      m_data.resize(m_used_size);
    }
    m_data.shrink_to_fit();
  }

  void clear() noexcept {
    m_data.clear();
    m_ext_data = nullptr;
    m_used_size = 0;
  }

  std::vector<uint8_t> release();

  // Transfer ownership of internal data to a string for zero-copy async transfer
  std::string move_to_string() {
    std::string s =
        (m_used_size > 0 && data() != nullptr)
            ? std::string(reinterpret_cast<const char *>(data()), m_used_size)
            : std::string();
    clear();
    return s;
  }

  // Implicit or explicit conversion to JSON string for string interoperability
  explicit operator std::string() const;

  Iterator begin(size_t ofs) const;
  Iterator end(size_t ofs) const;

private:
  friend class Iterator;
  friend class Value;

  // Internal implementation of set operations (C-style logic)
  // Returns the offset of the value data in the buffer
  size_t set_impl(size_t ofs, std::string_view key, uint32_t key_hash,
                  size_t val_len, const void *val_ptr, Type type,
                  bool is_append = false);

  // Splits a full child node
  void split_child(size_t parent_ofs, int index, size_t child_ofs);

  // Initializer helper
  void init_structure(Type type);

  // These are kept from the original private section
  void arr_append_impl(size_t ofs, size_t val_len, const void *val_ptr,
                       Type type);
  const std::byte *get_impl(size_t ofs, std::string_view key, uint32_t hash,
                            Type &type, bool is_array_op = false) const;
  const std::byte *arr_get_impl(size_t ofs, uint32_t index, Type &type) const;

  size_t bound_size() const noexcept { return m_used_size; }
  void materialize_if_view();

  std::vector<uint8_t> m_data;          // The raw buffer
  const uint8_t *m_ext_data = nullptr;  // External non-owning view pointer
  size_t m_used_size = 0;               // Currently used bytes
};

} // namespace lite3cpp

#endif // LITE3CPP_BUFFER_HPP