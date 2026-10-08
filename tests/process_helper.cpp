// Le programme que lancent les tests de runProcess (tests/process_test.cpp). Un programme de la
// cible, compilé avec les tests : le même sous Linux et sous Windows. Le cmake de l'hôte n'y ferait
// pas l'affaire, puisqu'un .exe lancé depuis WSL ne sait pas exécuter un binaire Linux (ADR-0035).
//
//   levain_process_helper echo <mots...>   écrit les mots séparés par une espace, puis « \n »
//   levain_process_helper exit <code>      sort avec ce code
//   levain_process_helper env <nom>        écrit la valeur de cette variable d'environnement, puis « \n »
#include <charconv>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include "levain/core/environment.hpp"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace
{

// EX_USAGE de sysexits.h : un code que les tests ne demandent pas, pour qu'une erreur d'appel ne se
// confonde pas avec le code de sortie voulu.
constexpr int UsageError = 64;
constexpr int MissingVariable = 65;

int echoArguments(std::span<char* const> words)
{
#ifdef _WIN32
    // Le mode texte de la CRT de Microsoft écrit « \r\n » pour chaque « \n » : sans ce réglage, la
    // sortie différerait de celle de Linux et le test ne serait plus le même.
    (void)_setmode(_fileno(stdout), _O_BINARY);
#endif
    for (std::size_t index = 0; index < words.size(); ++index)
    {
        if (index > 0)
        {
            std::fputc(' ', stdout);
        }
        std::fputs(words[index], stdout);
    }
    std::fputc('\n', stdout);
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    const std::span<char* const> arguments{argv, static_cast<std::size_t>(argc)};
    if (arguments.size() >= 2 && std::string_view{arguments[1]} == "echo")
    {
        return echoArguments(arguments.subspan(2));
    }
    if (arguments.size() == 3 && std::string_view{arguments[1]} == "env")
    {
        // Absente, la variable rend un code à elle, que le test distingue d'une valeur vide.
        std::optional<std::string> value = levain::core::environmentVariable(arguments[2]);
        if (!value)
        {
            return MissingVariable;
        }
        char* const words[] = {value->data()};
        return echoArguments(words);
    }
    if (arguments.size() == 3 && std::string_view{arguments[1]} == "exit")
    {
        const std::string_view code{arguments[2]};
        int exitCode = 0;
        if (std::from_chars(code.data(), code.data() + code.size(), exitCode).ec == std::errc{})
        {
            return exitCode;
        }
    }
    std::fputs("usage : levain_process_helper echo <mots...> | exit <code> | env <nom>\n", stderr);
    return UsageError;
}
