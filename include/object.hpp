#ifndef LITE3CPP_OBJECT_HPP
#define LITE3CPP_OBJECT_HPP

#include "value.hpp"

namespace lite3cpp {

class Object : public Value {
public:
  using Value::Value;
  bool contains(std::string_view key) const;
};

} // namespace lite3cpp

#endif // LITE3CPP_OBJECT_HPP
