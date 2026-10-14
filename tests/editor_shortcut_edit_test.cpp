// Changer la table des raccourcis (ADR-0036, décision 13, morceau 16) : de la logique pure. La
// disposition du clavier est un paramètre (le QWERTY du banc d'essai, puis un AZERTY qui échange W
// et Z, A et Q), la fenêtre n'est pas dessinée.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include <doctest/doctest.h>

#include "levain/editor/shortcut_edit.hpp"
#include "levain/platform/input_script.hpp"

using namespace levain::editor;
using levain::platform::KeyModifiers;

namespace
{

constexpr std::uint16_t ScanA = 4, ScanQ = 20, ScanW = 26, ScanZ = 29;
constexpr KeyModifiers Ctrl{.ctrl = true};
constexpr KeyModifiers CtrlShift{.ctrl = true, .shift = true};

std::uint32_t azertyAt(std::uint16_t scancode)
{
    switch (scancode)
    {
    case ScanA:
        return 'q';
    case ScanQ:
        return 'a';
    case ScanW:
        return 'z';
    case ScanZ:
        return 'w';
    default:
        return levain::platform::qwertyKeyAtPosition(scancode);
    }
}

// Des pointeurs de fonction constants, pas des `std::function` globales : leur construction
// pourrait lever (`bugprone-throwing-static-initialization`).
constexpr auto Qwerty = &levain::platform::qwertyKeyAtPosition;
constexpr auto Azerty = &azertyAt;

std::array<Chord, 2> chordsOf(const ShortcutTable& table, Action action)
{
    const Binding* binding = bindingOf(table, action);
    REQUIRE(binding != nullptr);
    return binding->chords;
}

/// Pose un accord à la main, sans contrôle : de quoi fabriquer une table en conflit.
void setChord(ShortcutTable& table, Action action, std::size_t slot, const Chord& chord)
{
    for (Binding& binding : table.bindings)
    {
        if (binding.action == action)
        {
            binding.chords.at(slot) = chord;
        }
    }
}

} // namespace

TEST_CASE("conflit : le même accord dans le même contexte est nommé")
{
    const ShortcutTable table = defaultShortcuts();
    const auto conflicts = conflictsOf(table, Action::Undo, letterChord('s', Ctrl), Qwerty);
    REQUIRE(conflicts.size() == 1);
    CHECK(conflicts.front().action == Action::Save);
    CHECK(conflictName(conflicts.front(), Qwerty) == "Ctrl+S : Enregistrer");
    // Sa propre place n'est pas un conflit : reposer l'accord qu'on a déjà ne dit rien.
    CHECK(conflictsOf(table, Action::Save, letterChord('s', Ctrl), Qwerty).empty());
    // Les modificateurs comptent : Ctrl+Maj+S n'est pas Ctrl+S.
    CHECK(conflictsOf(table, Action::Undo, letterChord('s', CtrlShift), Qwerty).empty());
}

TEST_CASE("conflit : le global contre tout autre contexte, le vol contre la Vue jamais")
{
    const ShortcutTable table = defaultShortcuts();
    // Temps réel est de la Vue, Enregistrer du global : Ctrl+S est pris.
    const auto global = conflictsOf(table, Action::RealTime, letterChord('s', Ctrl), Qwerty);
    REQUIRE(global.size() == 1);
    CHECK(global.front().action == Action::Save);
    // Et dans l'autre sens : Enregistrer sur Ctrl+R (Temps réel, de la Vue).
    const auto reverse = conflictsOf(table, Action::Save, letterChord('r', Ctrl), Qwerty);
    REQUIRE(reverse.size() == 1);
    CHECK(reverse.front().action == Action::RealTime);
    // Avancer (le vol) sur la place du W, que prend aussi l'outil Déplacer (la Vue) : voulu.
    CHECK(conflictsOf(table, Action::MoveForward, positionChord(ScanW), Qwerty).size() == 0);
    CHECK(conflictsIn(table, Qwerty).empty());
    CHECK(conflictsIn(table, Azerty).empty());
}

TEST_CASE("conflit : une position contre une lettre se compare par la disposition")
{
    const ShortcutTable table = defaultShortcuts();
    // « Z » sans modificateur : sur un AZERTY c'est la position du W (Avancer et l'outil Déplacer).
    const Chord z = letterChord('z');
    const auto azerty = conflictsOf(table, Action::Undo, z, Azerty);
    REQUIRE(azerty.size() == 2);
    CHECK(azerty.front().action == Action::MoveForward);
    CHECK(azerty.back().action == Action::ToolMove);
    // Sur un QWERTY la lettre Z est la position 29, que rien ne prend.
    CHECK(conflictsOf(table, Action::Undo, z, Qwerty).empty());
    // L'autre sens : Annuler sur la lettre W. La position du W est la touche Z d'un AZERTY, donc
    // libre là, et prise sur un QWERTY.
    ShortcutTable withW = table;
    setChord(withW, Action::Undo, 0, letterChord('w'));
    const Chord forward = positionChord(ScanW);
    CHECK(conflictsOf(withW, Action::MoveForward, forward, Azerty).empty());
    const auto qwerty = conflictsOf(withW, Action::MoveForward, forward, Qwerty);
    REQUIRE(qwerty.size() == 1);
    CHECK(qwerty.front().action == Action::Undo);
}

TEST_CASE("conflit né d'un changement de disposition : trouvé, jamais rebranché")
{
    ShortcutTable table = defaultShortcuts();
    setChord(table, Action::Undo, 0, letterChord('z')); // libre sur un QWERTY
    CHECK(conflictsIn(table, Qwerty).empty());
    const auto born = conflictsIn(table, Azerty);
    REQUIRE(born.size() == 2); // l'outil Déplacer et Avancer, à la position du W
    CHECK(born.front().first.action == Action::MoveForward);
    CHECK(born.front().second.action == Action::Undo);
    CHECK(chordsOf(table, Action::MoveForward)[0] == positionChord(ScanW)); // rien n'a bougé
}
