#ifndef LITE3CPP_HASH_HPP
#define LITE3CPP_HASH_HPP

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace lite3cpp {
namespace utils {

constexpr uint32_t djb2_hash(std::string_view key) noexcept {
  uint32_t hash = 5381;
  for (char c : key) {
    hash = ((hash << 5) + hash) + static_cast<uint32_t>(static_cast<int>(c));
  }
  return hash;
}

} // namespace utils

struct HashedKey {
  std::string_view key{};
  uint32_t hash{5381};

  constexpr HashedKey() noexcept = default;

  constexpr HashedKey(const char *s) noexcept
      : key(s != nullptr ? std::string_view(s) : std::string_view{}),
        hash(utils::djb2_hash(key)) {}

  constexpr HashedKey(std::string_view sv) noexcept
      : key(sv), hash(utils::djb2_hash(sv)) {}

  template <typename S>
    requires(!std::is_same_v<std::remove_cvref_t<S>, HashedKey> &&
             !std::is_convertible_v<const S &, const char *> &&
             !std::is_same_v<std::remove_cvref_t<S>, std::string_view> &&
             std::is_convertible_v<const S &, std::string_view>)
  constexpr HashedKey(const S &s) noexcept
      : key(std::string_view(s)), hash(utils::djb2_hash(key)) {}

  constexpr HashedKey(std::string_view sv, uint32_t precomputed) noexcept
      : key(sv), hash(precomputed) {}
};

namespace literals {

consteval HashedKey operator""_hk(const char *s, std::size_t len) noexcept {
  std::string_view sv(s, len);
  return HashedKey(sv, utils::djb2_hash(sv));
}

} // namespace literals
} // namespace lite3cpp

#endif // LITE3CPP_HASH_HPP
