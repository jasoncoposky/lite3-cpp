#ifndef LITE3CPP_JSON_HPP
#define LITE3CPP_JSON_HPP

#include "buffer.hpp"
#include <string>
#include <string_view>

namespace lite3cpp::lite3_json {

    std::string to_json_string(const Buffer& buffer, size_t ofs = 0);
    Buffer from_json_string(std::string_view json_str);
    inline Buffer from_json_string(const std::string& json_str) {
        return from_json_string(std::string_view(json_str));
    }
    inline Buffer from_json_string(const char* json_str) {
        return from_json_string(json_str ? std::string_view(json_str) : std::string_view{});
    }

} // namespace lite3cpp::lite3_json

#endif // LITE3CPP_JSON_HPP