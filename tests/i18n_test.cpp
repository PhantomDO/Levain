#include <format>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>

#include "levain/core/i18n.hpp"

using namespace levain::core;

namespace
{

std::string gpuText(const Catalog& catalog)
{
    const double ms = 1.5;
    return translateFormat(catalog, {}, "GPU : {:.3f} ms", std::make_format_args(ms));
}

} // namespace

TEST_CASE("une traduction absente ou vide laisse le français, un homonyme garde son contexte")
{
    const Catalog catalog{.entries = {{"Fichier", "File"},
                                      {"Vide", ""},
                                      {catalogKey("navigation", "Retour"), "Back"},
                                      {catalogKey("fonction", "Retour"), "Return value"}}};
    CHECK(std::string_view{translate(catalog, {}, "Fichier")} == "File");
    CHECK(std::string_view{translate(catalog, {}, "Inconnu")} == "Inconnu");
    CHECK(std::string_view{translate(catalog, {}, "Vide")} == "Vide");  // jamais un texte vide
    CHECK(std::string_view{translate({}, {}, "Fichier")} == "Fichier"); // le catalogue vide
    CHECK(std::string_view{translate(catalog, "navigation", "Retour")} == "Back");
    CHECK(std::string_view{translate(catalog, "fonction", "Retour")} == "Return value");
    CHECK(std::string_view{translate(catalog, {}, "Retour")} == "Retour"); // sans contexte
}

TEST_CASE("une traduction aux champs fautifs affiche le français, avec ses valeurs")
{
    Catalog catalog{.entries = {{"GPU : {:.3f} ms", "GPU: {:.3f} ms {{x}}"}}};
    CHECK(gpuText(catalog) == "GPU: 1.500 ms {x}"); // des accolades doublées sont du texte
    CHECK(placeholdersOf("{} sur {:.2f}") == std::vector<std::string>{"{}", "{:.2f}"});
    for (const char* faulty : {"GPU: {} ms", "GPU: {:.3f} ms {}", "GPU: ms", "GPU: {:.3f ms",
                               "GPU: } {:.3f}", "GPU: {0:.3f} ms", "GPU: {:{}} ms"})
    {
        catalog.entries["GPU : {:.3f} ms"] = faulty;
        INFO("traduction : " << std::string{faulty});
        CHECK(gpuText(catalog) == "GPU : 1.500 ms");
    }
}
