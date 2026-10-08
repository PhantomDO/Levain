#include <string>

#include <doctest/doctest.h>

#include "levain/core/environment.hpp"

TEST_CASE("environmentVariable trouve PATH, que tout système définit")
{
    // Sous Node, c'est l'environnement par défaut d'Emscripten qui le définit (« / »).
    const std::string path = levain::core::environmentVariable("PATH").value_or("");
    CHECK_FALSE(path.empty());
    // `_dupenv_s` compte le '\0' final dans sa taille : une copie de `size` octets le garderait.
    CHECK(path.find('\0') == std::string::npos);
}

TEST_CASE("environmentVariable rend std::nullopt pour une variable absente")
{
    CHECK_FALSE(levain::core::environmentVariable("LEVAIN_VARIABLE_QUI_N_EXISTE_PAS").has_value());
}
