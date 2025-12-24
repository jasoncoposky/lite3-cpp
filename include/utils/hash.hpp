#ifndef LITE3_HASH_HPP
#define LITE3_HASH_HPP

#include <cstdint>
#include <string_view>

namespace lite3::utils {

    uint32_t djb2_hash(std::string_view key);

} // namespace lite3::utils

#endif // LITE3_HASH_HPP
