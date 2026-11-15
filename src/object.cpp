#include "object.hpp"
#include "buffer.hpp"

namespace lite3cpp {

bool Object::contains(HashedKey key) const noexcept {
  return m_buffer != nullptr &&
         m_buffer->get_type_noexcept(m_offset, key) != Type::Invalid;
}

} // namespace lite3cpp
