#ifndef LITE3CPP_EXCEPTION_HPP
#define LITE3CPP_EXCEPTION_HPP

#include <stdexcept>
#include <string>

namespace lite3cpp {

    class exception : public std::runtime_error {
    public:
        explicit exception(const std::string& what) : std::runtime_error(what) {}
    };

} // namespace lite3cpp

#endif // LITE3CPP_EXCEPTION_HPP
