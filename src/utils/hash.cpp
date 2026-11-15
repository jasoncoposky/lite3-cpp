#include "utils/hash.hpp"

namespace lite3cpp {
namespace {

using namespace lite3cpp::literals;

static_assert(utils::djb2_hash("") == 5381u);
static_assert(""_hk.hash == 5381u);
static_assert("objPath"_hk.hash == utils::djb2_hash("objPath"));
static_assert(utils::djb2_hash("\x80") ==
              ((5381u << 5) + 5381u) +
                  static_cast<uint32_t>(static_cast<int>('\x80')));

} // namespace
} // namespace lite3cpp
