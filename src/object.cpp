#include "object.hpp"
#include "buffer.hpp"

namespace lite3cpp {

bool Object::contains(std::string_view key) const {
  try {
    m_buffer->get_type(m_offset, key);
    return true;
  } catch (...) {
    return false;
  }
}

} // namespace lite3cpp
