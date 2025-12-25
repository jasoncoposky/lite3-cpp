#ifndef LITE3CPP_HASH_HPP
#define LITE3CPP_HASH_HPP

#include <cstdint>
#include <string_view>

namespace lite3cpp::utils {

    uint32_t djb2_hash(std::string_view key);

} // namespace lite3cpp::utils

#endif // LITE3CPP_HASH_HPP
