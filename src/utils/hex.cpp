#include "hex.hpp"
#include <sstream>
#include <iomanip>

namespace lite3cpp::utils {

// Helper to convert a hex character to its integer value
static unsigned char hex_char_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    throw std::runtime_error("Invalid hex character");
}

std::vector<std::byte> hex_decode(std::string_view hex_string) {
    if (hex_string.length() % 2 != 0) {
        throw std::runtime_error("Hex string length must be even.");
    }

    std::vector<std::byte> bytes;
    bytes.reserve(hex_string.length() / 2);

    for (size_t i = 0; i < hex_string.length(); i += 2) {
        unsigned char high_nibble = hex_char_to_int(hex_string[i]);
        unsigned char low_nibble = hex_char_to_int(hex_string[i+1]);
        bytes.push_back(static_cast<std::byte>((high_nibble << 4) | low_nibble));
    }
    return bytes;
}

} // namespace lite3cpp::utils