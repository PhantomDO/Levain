#include <doctest/doctest.h>

#include "levain/core/environment.hpp"

TEST_CASE("environmentVariable trouve PATH, que tout système définit")
{
    CHECK_FALSE(levain::core::environmentVariable("PATH").value_or("").empty());
}

TEST_CASE("environmentVariable rend std::nullopt pour une variable absente")
{
    CHECK_FALSE(levain::core::environmentVariable("LEVAIN_VARIABLE_QUI_N_EXISTE_PAS").has_value());
}
