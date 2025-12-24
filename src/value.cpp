#include "value.hpp"
#include "buffer.hpp"
#include <cstring>

namespace lite3 {

    void Value::write(Buffer& buffer, size_t& offset, Type type, const void* data, size_t size) {
        // Write type
        buffer.m_data[offset++] = static_cast<std::byte>(type);

        // Write data
        if (type == Type::String || type == Type::Bytes) {
            uint32_t len = size;
            if (buffer.m_data.size() < offset + sizeof(len) + len + (type == Type::String ? 1 : 0)) { // +1 for null terminator if string
                buffer.m_data.resize(offset + sizeof(len) + len + (type == Type::String ? 1 : 0));
            }
            std::memcpy(buffer.m_data.data() + offset, &len, sizeof(len));
            offset += sizeof(len);
            if(data) {
                std::memcpy(buffer.m_data.data() + offset, data, len);
                offset += len;
            }
            if(type == Type::String) {
                 buffer.m_data[offset++] = std::byte{'\0'}; // Append null terminator and increment offset
            }
        } else {
            if (buffer.m_data.size() < offset + size) {
                buffer.m_data.resize(offset + size);
            }
            if(data) {
                std::memcpy(buffer.m_data.data() + offset, data, size);
                offset += size;
            }
        }
    }

    size_t Value::read_size(const Buffer& buffer, size_t offset) {
        Type type = static_cast<Type>(buffer.m_data[offset]);
        offset++;
        switch (type) {
            case Type::Null:
                return 0;
            case Type::Bool:
                return sizeof(bool);
            case Type::Int64:
                return sizeof(int64_t);
            case Type::Float64:
                return sizeof(double);
            case Type::Bytes:
            case Type::String: {
                uint32_t size = 0;
                std::memcpy(&size, buffer.m_data.data() + offset, sizeof(uint32_t));
                return sizeof(uint32_t) + size + (type == Type::String ? 1 : 0); // +1 for null terminator if string
            }
            case Type::Object:
            case Type::Array:
                return config::node_size; // Simplified for now
            default:
                return 0;
        }
    }

} // namespace lite3