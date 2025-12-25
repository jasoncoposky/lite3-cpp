#include "utils/hash.hpp"

namespace lite3cpp::utils {

    uint32_t djb2_hash(std::string_view key) {
        uint32_t hash = 5381;
        for (char c : key) {
            hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
        }
        return hash;
    }

} // namespace lite3cpp::utils
