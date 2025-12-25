#ifndef LITE3CPP_VALUE_HPP
#define LITE3CPP_VALUE_HPP

#include <cstdint>
#include <cstddef>
#include "node.hpp"

namespace lite3cpp {

    class Buffer;

    class Value {
    public:
        static void write(Buffer& buffer, size_t& offset, Type type, const void* data, size_t size);
        static size_t read_size(const Buffer& buffer, size_t offset);
    };

} // namespace lite3cpp

#endif // LITE3CPP_VALUE_HPP
