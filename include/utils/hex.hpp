#ifndef LITE3CPP_UTILS_HEX_HPP
#define LITE3CPP_UTILS_HEX_HPP

#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace lite3cpp::utils {

// Decodes a hex string into a vector of bytes.
// Throws std::runtime_error if the input string is not a valid hex string.
std::vector<std::byte> hex_decode(std::string_view hex_string);

// Encodes a span of bytes into a lowercase hexadecimal string using a 512-byte LUT.
std::string hex_encode(std::span<const std::byte> data);

} // namespace lite3cpp::utils

#endif // LITE3CPP_UTILS_HEX_HPP
