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
  Buffer();
  explicit Buffer(size_t initial_size);
  explicit Buffer(std::vector<uint8_t> data);

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
  const uint8_t *data() const { return m_data.data(); }
  size_t size() const { return m_data.size(); }
  void reserve(size_t capacity) { m_data.reserve(capacity); }
  size_t capacity() const { return m_data.capacity(); }

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

  // Helper to ensure buffer has enough space for additional bytes + alignment
  // Returns pointer to current data (invalidated on resize)
  void ensure_capacity(size_t required_bytes);

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

  std::vector<uint8_t> m_data; // The raw buffer
  size_t m_used_size;          // Currently used bytes
};

} // namespace lite3cpp

#endif // LITE3CPP_BUFFER_HPP