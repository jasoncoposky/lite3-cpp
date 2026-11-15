#include "hex.hpp"
#include <array>
#include <cstring>

namespace lite3cpp::utils {

namespace {
constexpr std::array<char, 512> make_hex_pairs() noexcept {
  constexpr char digits[] = "0123456789abcdef";
  std::array<char, 512> lut{};
  for (size_t i = 0; i < 256; ++i) {
    lut[i * 2] = digits[(i >> 4) & 0x0Fu];
    lut[i * 2 + 1] = digits[i & 0x0Fu];
  }
  return lut;
}
constexpr std::array<char, 512> kHexPairs = make_hex_pairs();

unsigned char hex_char_to_int(char c) {
  if (c >= '0' && c <= '9') return static_cast<unsigned char>(c - '0');
  if (c >= 'a' && c <= 'f') return static_cast<unsigned char>(c - 'a' + 10);
  if (c >= 'A' && c <= 'F') return static_cast<unsigned char>(c - 'A' + 10);
  throw std::runtime_error("Invalid hex character");
}
} // namespace

std::vector<std::byte> hex_decode(std::string_view hex_string) {
  if (hex_string.length() % 2 != 0) {
    throw std::runtime_error("Hex string length must be even.");
  }

  std::vector<std::byte> bytes;
  bytes.reserve(hex_string.length() / 2);

  for (size_t i = 0; i < hex_string.length(); i += 2) {
    unsigned char high_nibble = hex_char_to_int(hex_string[i]);
    unsigned char low_nibble = hex_char_to_int(hex_string[i + 1]);
    bytes.push_back(static_cast<std::byte>((high_nibble << 4) | low_nibble));
  }
  return bytes;
}

std::string hex_encode(std::span<const std::byte> data) {
  if (data.size() > (std::string{}.max_size() / 2)) {
    throw std::length_error("hex_encode size overflow");
  }
  std::string out(data.size() * 2, '\0');
  char *dst = out.data();
  for (size_t i = 0; i < data.size(); ++i) {
    const auto idx =
        static_cast<size_t>(std::to_integer<unsigned char>(data[i])) * 2;
    std::memcpy(dst + i * 2, &kHexPairs[idx], 2);
  }
  return out;
}

} // namespace lite3cpp::utils
