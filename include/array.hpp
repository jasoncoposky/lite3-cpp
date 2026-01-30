#ifndef LITE3CPP_ARRAY_HPP
#define LITE3CPP_ARRAY_HPP

#include "value.hpp"

namespace lite3cpp {

class Array : public Value {
public:
  using Value::Value;
  void push_back(bool val);
  void push_back(int64_t val);
  void push_back(int val) { push_back(static_cast<int64_t>(val)); }
  void push_back(double val);
  void push_back(std::string_view val);
  void push_back(const char *val) { push_back(std::string_view(val)); }
  size_t size() const;
};

} // namespace lite3cpp

#endif // LITE3CPP_ARRAY_HPP
