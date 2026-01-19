#include "value.hpp"
#include "buffer.hpp"
#include <algorithm> // for max
#include <cstring>


namespace lite3cpp {

void Value::write(Buffer &buffer, size_t &offset, Type type, const void *data,
                  size_t size) {
  // Write type
  if (offset >= buffer.m_data.size())
    buffer.m_data.resize(std::max(buffer.m_data.size() * 2, offset + 1));
  buffer.m_data[offset++] = static_cast<uint8_t>(type);

  // Write data
  if (type == Type::String || type == Type::Bytes) {
    uint32_t len = static_cast<uint32_t>(size);
    size_t required =
        offset + sizeof(len) + len + (type == Type::String ? 1 : 0);
    if (buffer.m_data.size() < required) {
      buffer.m_data.resize(std::max(buffer.m_data.size() * 2, required));
    }
    std::memcpy(buffer.m_data.data() + offset, &len, sizeof(len));
    offset += sizeof(len);
    if (data && len > 0) {
      std::memcpy(buffer.m_data.data() + offset, data, len);
      offset += len;
    }
    if (type == Type::String) {
      buffer.m_data[offset++] = 0; // Null terminator
    }
  } else {
    if (buffer.m_data.size() < offset + size) {
      buffer.m_data.resize(std::max(buffer.m_data.size() * 2, offset + size));
    }
    if (data && size > 0) {
      std::memcpy(buffer.m_data.data() + offset, data, size);
    }
    offset += size;
  }
}

size_t Value::read_size(const Buffer &buffer, size_t offset) {
  if (offset >= buffer.m_data.size())
    return 0;
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
    if (offset + sizeof(uint32_t) > buffer.m_data.size())
      return 0;
    uint32_t size = 0;
    std::memcpy(&size, buffer.m_data.data() + offset, sizeof(uint32_t));
    return sizeof(uint32_t) + size + (type == Type::String ? 1 : 0);
  }
  case Type::Object:
  case Type::Array:
    return config::node_size;
  default:
    return 0;
  }
}

} // namespace lite3cpp