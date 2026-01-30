#ifndef LITE3CPP_DOCUMENT_HPP
#define LITE3CPP_DOCUMENT_HPP

#include "array.hpp"
#include "buffer.hpp"
#include "object.hpp"
#include "value.hpp"


namespace lite3cpp {

class Document {
public:
  Document();
  explicit Document(size_t initial_size);
  explicit Document(Buffer buf);

  Object root_obj();
  Array root_arr();

  Buffer &buffer() { return m_buffer; }
  const Buffer &buffer() const { return m_buffer; }

private:
  Buffer m_buffer;
};

} // namespace lite3cpp

#endif // LITE3CPP_DOCUMENT_HPP
