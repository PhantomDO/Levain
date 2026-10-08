#include "levain/core/environment.hpp"

#include <cstdlib>
#include <memory>

namespace levain::core
{

std::optional<std::string> environmentVariable(std::string_view name)
{
    const std::string terminated{name}; // un `string_view` n'a pas de '\0' final
#ifdef _WIN32
    // `_dupenv_s` rend une copie, libérée même si la copie en `std::string` lève ; absente, la
    // variable laisse le pointeur à nullptr.
    char* raw = nullptr;
    if (_dupenv_s(&raw, nullptr, terminated.c_str()) != 0 || raw == nullptr)
    {
        return std::nullopt;
    }
    const auto release = [](char* value) { std::free(value); };
    const std::unique_ptr<char, decltype(release)> value{raw, release};
    return std::string{value.get()};
#else
    const char* value = std::getenv(terminated.c_str());
    return value == nullptr ? std::nullopt : std::optional<std::string>{value};
#endif
}

} // namespace levain::core
