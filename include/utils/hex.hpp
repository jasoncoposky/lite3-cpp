#ifndef LITE3CPP_UTILS_HEX_HPP
#define LITE3CPP_UTILS_HEX_HPP

#include <string>
#include <vector>
#include <stdexcept>
#include <cstddef> // For std::byte

namespace lite3cpp::utils {

// Decodes a hex string into a vector of bytes.
// Throws std::runtime_error if the input string is not a valid hex string.
std::vector<std::byte> hex_decode(std::string_view hex_string);

} // namespace lite3cpp::utils

#endif // LITE3CPP_UTILS_HEX_HPP