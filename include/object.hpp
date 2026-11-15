#ifndef LITE3CPP_OBJECT_HPP
#define LITE3CPP_OBJECT_HPP

#include "utils/hash.hpp"
#include "value.hpp"

namespace lite3cpp {

class Object : public Value {
public:
  using Value::Value;
  bool contains(HashedKey key) const noexcept;
};

} // namespace lite3cpp

#endif // LITE3CPP_OBJECT_HPP
