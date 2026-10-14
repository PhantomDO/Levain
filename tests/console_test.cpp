#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "levain/core/log_ring.hpp"
#include "levain/editor/console.hpp"

using levain::core::LogEntry;
using levain::core::LogLevel;
using levain::editor::ConsoleFilter;
using levain::editor::ConsoleRow;

namespace
{

LogEntry entryOf(LogLevel level, std::string text, std::string category = "test")
{
    return {
        .level = level, .category = std::move(category), .text = std::move(text), .seconds = 1.5};
}

ConsoleFilter showing(bool info, bool warning, bool error, std::string_view search = {})
{
    return {.info = info, .warning = warning, .error = error, .search = search};
}

std::vector<std::size_t> shownIndices(const std::vector<LogEntry>& entries,
                                      const ConsoleFilter& filter, bool collapse = false)
{
    std::vector<ConsoleRow> rows;
    levain::editor::listConsoleRows(entries, filter, collapse, rows);
    std::vector<std::size_t> indices;
    indices.reserve(rows.size());
    for (const ConsoleRow& row : rows)
    {
        indices.push_back(row.entry);
    }
    return indices;
}

} // namespace

TEST_CASE(
    "la recherche ignore la casse ASCII mais pas les accents, et une recherche vide trouve tout")
{
    using levain::editor::containsIgnoringAsciiCase;
    CHECK(containsIgnoringAsciiCase("Erreur du GPU", "gpu"));
    CHECK(containsIgnoringAsciiCase("Erreur du GPU", "ERREUR"));
    CHECK(containsIgnoringAsciiCase("", ""));
    CHECK(containsIgnoringAsciiCase("abc", ""));
    CHECK_FALSE(containsIgnoringAsciiCase("abc", "abcd"));
    CHECK_FALSE(containsIgnoringAsciiCase("abc", "d"));
    CHECK_FALSE(
        containsIgnoringAsciiCase("Écran", "écran")); // pliage Unicode : hors de ce premier jet
    CHECK(containsIgnoringAsciiCase("écran", "écran"));
}

TEST_CASE("les trois niveaux filtrent, Trace et Debug avec Info et Critical avec Error")
{
    const std::vector<LogEntry> entries{
        entryOf(LogLevel::Trace, "t"), entryOf(LogLevel::Debug, "d"),
        entryOf(LogLevel::Info, "i"),  entryOf(LogLevel::Warning, "w"),
        entryOf(LogLevel::Error, "e"), entryOf(LogLevel::Critical, "c")};
    using Indices = std::vector<std::size_t>;
    CHECK(shownIndices(entries, {}) == Indices{0, 1, 2, 3, 4, 5});
    CHECK(shownIndices(entries, showing(false, true, true)) == Indices{3, 4, 5});
    CHECK(shownIndices(entries, showing(true, false, true)) == Indices{0, 1, 2, 4, 5});
    CHECK(shownIndices(entries, showing(true, true, false)) == Indices{0, 1, 2, 3});
    CHECK(shownIndices(entries, showing(false, false, false)).empty());
}

TEST_CASE("la recherche trouve le texte ou la catégorie, et se combine avec les niveaux")
{
    const std::vector<LogEntry> entries{
        entryOf(LogLevel::Error, "forme refusée par Jolt", "physics"),
        entryOf(LogLevel::Info, "texture chargée", "assets"),
        entryOf(LogLevel::Warning, "cache de pipelines absent", "gpu"),
        entryOf(LogLevel::Error, "fichier introuvable", "assets")};
    using Indices = std::vector<std::size_t>;
    CHECK(shownIndices(entries, showing(true, true, true, "jolt")) == Indices{0});
    CHECK(shownIndices(entries, showing(true, true, true, "ASSETS")) ==
          Indices{1, 3}); // la catégorie
    CHECK(shownIndices(entries, showing(false, true, true, "assets")) == Indices{3});
    CHECK(shownIndices(entries, showing(true, true, true, "rien de tel")).empty());
}

TEST_CASE(
    "Réduire compte les répétitions qui se suivent parmi les lignes montrées, et elles seules")
{
    const std::vector<LogEntry> entries{
        entryOf(LogLevel::Error, "panne"),
        entryOf(LogLevel::Error, "panne"),
        entryOf(LogLevel::Error, "panne"),
        entryOf(LogLevel::Warning, "panne"), // même texte, autre niveau : une autre ligne
        entryOf(LogLevel::Error, "panne"),
        entryOf(LogLevel::Error, "panne", "gpu")}; // autre catégorie : une autre ligne
    std::vector<ConsoleRow> rows;
    levain::editor::listConsoleRows(entries, {}, false, rows);
    CHECK(rows.size() == 6);

    levain::editor::listConsoleRows(entries, {}, true, rows);
    REQUIRE(rows.size() == 4);
    CHECK(rows[0].repeats == 3);
    CHECK(rows[1].repeats == 1);
    CHECK(rows[2].repeats == 1);
    CHECK(rows[3].repeats == 1);
    CHECK(rows[0].entry == 0); // la première du groupe la représente

    // Un message d'un autre niveau, caché, ne sépare plus deux erreurs identiques.
    const std::vector<LogEntry> separated{
        entryOf(LogLevel::Error, "x"), entryOf(LogLevel::Info, "y"), entryOf(LogLevel::Error, "x")};
    levain::editor::listConsoleRows(separated, {}, true, rows);
    CHECK(rows.size() == 3);
    levain::editor::listConsoleRows(separated, showing(false, true, true), true, rows);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].repeats == 2);
}

TEST_CASE("la liste suit le bout tant qu'on y est, et plus quand on est remonté")
{
    using levain::editor::shouldFollowTail;
    CHECK(shouldFollowTail(0.0f, 0.0f)); // rien à défiler
    CHECK(shouldFollowTail(400.0f, 400.0f));
    CHECK(shouldFollowTail(399.5f, 400.0f));       // un défilement fractionnaire
    CHECK_FALSE(shouldFollowTail(300.0f, 400.0f)); // remonté
    CHECK_FALSE(shouldFollowTail(0.0f, 400.0f));
}

TEST_CASE("chaque case porte le compte de son niveau, et la ligne son temps, sa catégorie et ses "
          "répétitions")
{
    const auto labels = levain::editor::filterLabels({.infos = 12, .warnings = 3, .errors = 7});
    CHECK(labels[0] == "Infos : 12");
    CHECK(labels[1] == "Avertissements : 3");
    CHECK(labels[2] == "Erreurs : 7");

    // Un message sur plusieurs lignes tient sur une seule, sans retour à la fin ni « \r ».
    CHECK(levain::editor::singleLine("a\r\nb\nc\n") == "a | b | c");
    CHECK(levain::editor::singleLine("") == "");

    const LogEntry entry = entryOf(LogLevel::Error, "panne", "gpu");
    CHECK(levain::editor::lineOf(entry, 1) == "[   1.500] [gpu] panne");
    CHECK(levain::editor::lineOf(entry, 3) == "[   1.500] [gpu] panne (×3)");
}
