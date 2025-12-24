#ifndef LITE3_EXCEPTION_HPP
#define LITE3_EXCEPTION_HPP

#include <stdexcept>
#include <string>

namespace lite3 {

    class exception : public std::runtime_error {
    public:
        explicit exception(const std::string& what) : std::runtime_error(what) {}
    };

} // namespace lite3

#endif // LITE3_EXCEPTION_HPP
