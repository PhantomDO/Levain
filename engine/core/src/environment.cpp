#include "levain/core/environment.hpp"

#include <cstddef>
#include <cstdlib>

namespace levain::core
{

std::optional<std::string> environmentVariable(std::string_view name)
{
    const std::string terminated{name}; // un `string_view` n'a pas de '\0' final
#ifdef _WIN32
    // `_dupenv_s` rend une copie, à libérer ; absente, la variable laisse `value` à nullptr.
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, terminated.c_str()) != 0 || value == nullptr)
    {
        return std::nullopt;
    }
    std::string copy{value};
    std::free(value);
    return copy;
#else
    const char* value = std::getenv(terminated.c_str());
    return value == nullptr ? std::nullopt : std::optional<std::string>{value};
#endif
}

} // namespace levain::core
