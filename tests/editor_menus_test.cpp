// La fenêtre Aide de l'éditeur (ADR-0036, décisions 12 et 13, morceau 12), ses lignes : chaque
// raccourci de la table, avec sa touche selon la disposition du clavier, et la recherche qui les
// filtre.

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "levain/editor/actions.hpp"
#include "levain/editor/menus.hpp"
#include "levain/platform/input_script.hpp"

using namespace levain::editor;

namespace
{

/// Un AZERTY : A et Q, W et Z échangés (comme tests/editor_shortcuts_test.cpp).
std::uint32_t azertyAt(std::uint16_t scancode)
{
    constexpr std::uint16_t A = 4, Q = 20, W = 26, Z = 29;
    switch (scancode)
    {
    case A:
        return 'q';
    case Q:
        return 'a';
    case W:
        return 'z';
    case Z:
        return 'w';
    default:
        return levain::platform::qwertyKeyAtPosition(scancode);
    }
}

const HelpRow* rowOf(const std::vector<HelpRow>& rows, Action action)
{
    const auto found = std::ranges::find(rows, action, &HelpRow::action);
    return found == rows.end() ? nullptr : &*found;
}

} // namespace

TEST_CASE("l'Aide liste chaque entrée de la table, avec sa touche selon la disposition")
{
    const ShortcutTable table = defaultShortcuts();
    const auto qwerty = helpRowsOf(table, levain::platform::qwertyKeyAtPosition);
    const auto azerty = helpRowsOf(table, azertyAt);
    REQUIRE(qwerty.size() == table.bindings.size());
    CHECK(azerty.size() == table.bindings.size());
    // Aucune action n'est oubliée.
    for (std::size_t i = 0; i < ActionCount; ++i)
    {
        CHECK(rowOf(qwerty, static_cast<Action>(i)) != nullptr);
    }
    // Avancer : W en QWERTY, Z en AZERTY (la touche à la place du W).
    CHECK(rowOf(qwerty, Action::MoveForward)->chords == "W");
    CHECK(rowOf(azerty, Action::MoveForward)->chords == "Z");
    CHECK(rowOf(azerty, Action::MoveLeft)->chords == "Q");
    // Un accord de lettre reste la lettre : Ctrl+S ne change pas avec la disposition.
    CHECK(rowOf(azerty, Action::Save)->chords == "Ctrl+S");
    // Rétablir a deux accords.
    CHECK(rowOf(qwerty, Action::Redo)->chords == "Ctrl+Y / Ctrl+Maj+Z");
    CHECK(rowOf(qwerty, Action::MoveForward)->context == "Vol (clic droit tenu)");
    CHECK(rowOf(qwerty, Action::Save)->context == "Partout");
    // Les touches d'Unreal et d'Unity, de la table des gestes.
    CHECK(rowOf(qwerty, Action::Play)->unreal == "Alt+P");
    CHECK(rowOf(qwerty, Action::Play)->unity == "Ctrl+P");
}

TEST_CASE("la recherche de l'Aide trouve par le nom, la touche, le contexte ou l'autre moteur")
{
    const auto rows = helpRowsOf(defaultShortcuts(), levain::platform::qwertyKeyAtPosition);
    const auto count = [&rows](std::string_view search)
    {
        return std::ranges::count_if(rows, [&](const HelpRow& row)
                                     { return helpRowMatches(row, search); });
    };
    CHECK(count("") == static_cast<std::ptrdiff_t>(rows.size()));
    CHECK(count("zzzzzz") == 0);
    CHECK(count("enregistrer") == 1);                            // la casse ne compte pas
    CHECK(count("ctrl+s") == 1);                                 // la touche
    CHECK(count("partout") >= 3);                                // le contexte
    CHECK(helpRowMatches(*rowOf(rows, Action::Play), "ctrl+p")); // la colonne Unity
}
