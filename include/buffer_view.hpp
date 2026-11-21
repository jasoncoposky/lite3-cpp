#ifndef LITE3CPP_BUFFER_VIEW_HPP
#define LITE3CPP_BUFFER_VIEW_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

#include "config.hpp"
#include "node.hpp"
#include "utils/hash.hpp"

namespace lite3cpp {

// Lightweight, non-owning, trivially-copyable/destructible read-only view
// over a raw Lite3 byte buffer. Ideal for zero-overhead cold-wrap lookups.
class BufferView {
public:
  constexpr BufferView() noexcept = default;
  constexpr BufferView(const uint8_t *ptr, size_t len) noexcept
      : m_data((ptr != nullptr && len > 0) ? ptr : nullptr),
        m_size((ptr != nullptr && len > 0) ? len : 0) {}
  constexpr explicit BufferView(std::span<const std::byte> bytes) noexcept
      : m_data((bytes.data() != nullptr && !bytes.empty())
                   ? reinterpret_cast<const uint8_t *>(bytes.data())
                   : nullptr),
        m_size((bytes.data() != nullptr && !bytes.empty()) ? bytes.size() : 0) {
  }

  constexpr const uint8_t *data() const noexcept { return m_data; }
  constexpr size_t size() const noexcept { return m_size; }
  constexpr bool empty() const noexcept { return m_size == 0; }

  bool get_bool(size_t ofs, const HashedKey &key) const;
  int64_t get_i64(size_t ofs, const HashedKey &key) const;
  double get_f64(size_t ofs, const HashedKey &key) const;
  std::string_view get_str(size_t ofs, const HashedKey &key) const;
  std::span<const std::byte> get_bytes(size_t ofs, const HashedKey &key) const;
  size_t get_obj(size_t ofs, const HashedKey &key) const;
  size_t get_arr(size_t ofs, const HashedKey &key) const;

  std::optional<bool> try_get_bool(size_t ofs,
                                   const HashedKey &key) const noexcept;
  std::optional<int64_t> try_get_i64(size_t ofs,
                                     const HashedKey &key) const noexcept;
  std::optional<double> try_get_f64(size_t ofs,
                                    const HashedKey &key) const noexcept;
  std::optional<std::string_view>
  try_get_str(size_t ofs, const HashedKey &key) const noexcept;
  std::optional<std::span<const std::byte>>
  try_get_bytes(size_t ofs, const HashedKey &key) const noexcept;
  std::optional<size_t> try_get_obj(size_t ofs,
                                    const HashedKey &key) const noexcept;
  std::optional<size_t> try_get_arr(size_t ofs,
                                    const HashedKey &key) const noexcept;

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
  std::optional<std::string_view>
  arr_try_get_str(size_t ofs, uint32_t index) const noexcept;
  std::optional<std::span<const std::byte>>
  arr_try_get_bytes(size_t ofs, uint32_t index) const noexcept;
  std::optional<size_t> arr_try_get_obj(size_t ofs,
                                        uint32_t index) const noexcept;
  std::optional<size_t> arr_try_get_arr(size_t ofs,
                                        uint32_t index) const noexcept;

  Type get_type(size_t ofs, const HashedKey &key) const;
  Type get_type_noexcept(size_t ofs, const HashedKey &key) const noexcept;
  Type arr_get_type(size_t ofs, uint32_t index) const;

private:
  const uint8_t *m_data = nullptr;
  size_t m_size = 0;
};

static_assert(std::is_trivially_copyable_v<BufferView>,
              "BufferView must be trivially copyable");
static_assert(std::is_trivially_destructible_v<BufferView>,
              "BufferView must be trivially destructible");

} // namespace lite3cpp

#endif // LITE3CPP_BUFFER_VIEW_HPP
