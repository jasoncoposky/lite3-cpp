#ifndef LITE3_VALUE_HPP
#define LITE3_VALUE_HPP

#include <cstdint>
#include <cstddef>
#include "node.hpp"

namespace lite3 {

    class Buffer;

    class Value {
    public:
        static void write(Buffer& buffer, size_t& offset, Type type, const void* data, size_t size);
        static size_t read_size(const Buffer& buffer, size_t offset);
    };

} // namespace lite3

#endif // LITE3_VALUE_HPP
