#ifndef LITE3CPP_BUFFER_HPP
#define LITE3CPP_BUFFER_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "buffer_view.hpp"
#include "config.hpp"
#include "exception.hpp"
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
  BufferView view() const noexcept { return BufferView(data(), size()); }

  void init_object();
  void init_array();

  void set_null(size_t ofs, const HashedKey& key);
  void set_bool(size_t ofs, const HashedKey& key, bool value);
  void set_i64(size_t ofs, const HashedKey& key, int64_t value);
  void set_f64(size_t ofs, const HashedKey& key, double value);
  void set_str(size_t ofs, const HashedKey& key, std::string_view value);
  void set_bytes(size_t ofs, const HashedKey& key,
                 std::span<const std::byte> value);
  std::span<std::byte> set_bytes_uninitialized(size_t ofs, const HashedKey& key,
                                               size_t len);
  size_t set_obj(size_t ofs, const HashedKey& key);
  size_t set_arr(size_t ofs, const HashedKey& key);

  void arr_append_null(size_t ofs);
  void arr_append_bool(size_t ofs, bool value);
  void arr_append_i64(size_t ofs, int64_t value);
  void arr_append_f64(size_t ofs, double value);
  void arr_append_str(size_t ofs, std::string_view value);
  void arr_append_bytes(size_t ofs, std::span<const std::byte> value);
  std::span<std::byte> arr_append_bytes_uninitialized(size_t ofs, size_t len);
  size_t arr_append_obj(size_t ofs);
  size_t arr_append_arr(size_t ofs);

  bool get_bool(size_t ofs, const HashedKey& key) const;
  int64_t get_i64(size_t ofs, const HashedKey& key) const;
  double get_f64(size_t ofs, const HashedKey& key) const;
  std::string_view get_str(size_t ofs, const HashedKey& key) const;
  std::span<const std::byte> get_bytes(size_t ofs, const HashedKey& key) const;
  size_t get_obj(size_t ofs, const HashedKey& key) const;
  size_t get_arr(size_t ofs, const HashedKey& key) const;

  std::optional<bool> try_get_bool(size_t ofs, const HashedKey& key) const noexcept;
  std::optional<int64_t> try_get_i64(size_t ofs, const HashedKey& key) const noexcept;
  std::optional<double> try_get_f64(size_t ofs, const HashedKey& key) const noexcept;
  std::optional<std::string_view> try_get_str(size_t ofs,
                                              const HashedKey& key) const noexcept;
  std::optional<std::span<const std::byte>>
  try_get_bytes(size_t ofs, const HashedKey& key) const noexcept;
  std::optional<size_t> try_get_obj(size_t ofs, const HashedKey& key) const noexcept;
  std::optional<size_t> try_get_arr(size_t ofs, const HashedKey& key) const noexcept;

  bool arr_get_bool(size_t ofs, uint32_t index) const;
  int64_t arr_get_i64(size_t ofs, uint32_t index) const;
  double arr_get_f64(size_t ofs, uint32_t index) const;
  std::string_view arr_get_str(size_t ofs, uint32_t index) const;
  std::span<const std::byte> arr_get_bytes(size_t ofs, uint32_t index) const;
  size_t arr_get_obj(size_t ofs, uint32_t index) const;
  size_t arr_get_arr(size_t ofs, uint32_t index) const;

  std::optional<bool> arr_try_get_bool(size_t ofs,
                                       uint32_t index) const noexcept;
  std::optional<int64_t> arr_try_get_i64(size_t ofs,
                                         uint32_t index) const noexcept;
  std::optional<double> arr_try_get_f64(size_t ofs,
                                        uint32_t index) const noexcept;
  std::optional<std::string_view> arr_try_get_str(size_t ofs,
                                                  uint32_t index) const noexcept;
  std::optional<std::span<const std::byte>>
  arr_try_get_bytes(size_t ofs, uint32_t index) const noexcept;
  std::optional<size_t> arr_try_get_obj(size_t ofs,
                                        uint32_t index) const noexcept;
  std::optional<size_t> arr_try_get_arr(size_t ofs,
                                        uint32_t index) const noexcept;

  Type arr_get_type(size_t ofs, uint32_t index) const;
  Type get_type(size_t ofs, const HashedKey& key) const;
  Type get_type_noexcept(size_t ofs, const HashedKey& key) const noexcept;

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

  size_t arr_append_impl(size_t ofs, size_t val_len, const void *val_ptr,
                         Type type);
  const std::byte *get_impl(size_t ofs, std::string_view key, uint32_t hash,
                            Type &type, bool is_array_op = false) const;
  const std::byte *get_impl_noexcept(size_t ofs, std::string_view key,
                                     uint32_t hash, Type &type,
                                     bool is_array_op = false,
                                     bool *max_height_exceeded = nullptr) const noexcept;
  const std::byte *arr_get_impl(size_t ofs, uint32_t index, Type &type) const;
  const std::byte *arr_get_impl_noexcept(size_t ofs, uint32_t index,
                                         Type &type) const noexcept;

  size_t bound_size() const noexcept { return m_used_size; }
  void materialize_if_view();

  std::vector<uint8_t> m_data;          // The raw buffer
  const uint8_t *m_ext_data = nullptr;  // External non-owning view pointer
  size_t m_used_size = 0;               // Currently used bytes
};

inline bool Iterator::is_modified() const noexcept {
  if (m_buffer->data() != m_initial_data ||
      m_buffer->bound_size() != m_initial_bound_size) [[unlikely]] {
    return true;
  }
  const auto *root_packed =
      reinterpret_cast<const PackedNodeLayout *>(m_initial_data);
  if (m_initial_buffer_generation !=
      ((root_packed->gen_type & NODE_GEN_MASK) >> NODE_GEN_SHIFT)) [[unlikely]] {
    return true;
  }
  if (m_depth >= 0 && m_stack[0].offset != 0) [[unlikely]] {
    if (m_stack[0].offset > m_initial_bound_size - sizeof(PackedNodeLayout)) {
      return true;
    }
    const auto *start_packed = reinterpret_cast<const PackedNodeLayout *>(
        m_initial_data + m_stack[0].offset);
    if (m_initial_node_generation !=
        ((start_packed->gen_type & NODE_GEN_MASK) >> NODE_GEN_SHIFT)) {
      return true;
    }
  }
  return false;
}

inline const Iterator::value_type &Iterator::operator*() const {
  if (!m_buffer) [[unlikely]]
    throw lite3cpp::exception("Invalid iterator");
  if (is_modified()) [[unlikely]] {
    throw lite3cpp::exception(
        "Iterator invalidated: Buffer modified during iteration.");
  }
  return m_current_value;
}

inline const Iterator::value_type *Iterator::operator->() const {
  if (!m_buffer) [[unlikely]]
    throw lite3cpp::exception("Invalid iterator");
  if (is_modified()) [[unlikely]] {
    throw lite3cpp::exception(
        "Iterator invalidated: Buffer modified during iteration.");
  }
  return &m_current_value;
}

inline std::string_view Iterator::key() const { return (**this).key; }

} // namespace lite3cpp

#endif // LITE3CPP_BUFFER_HPP
