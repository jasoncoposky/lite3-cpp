#ifndef LITE3CPP_JSON_HPP
#define LITE3CPP_JSON_HPP

#include "buffer.hpp"
#include <string>

namespace lite3cpp::lite3_json {

    std::string to_json_string(const Buffer& buffer, size_t ofs);
    Buffer from_json_string(const std::string& json_str);

} // namespace lite3cpp::lite3_json

#endif // LITE3CPP_JSON_HPP