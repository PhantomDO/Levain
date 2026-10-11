// La table des raccourcis (ADR-0036, décision 13, morceau 10) : de la logique pure. La disposition
// du clavier est un paramètre : le QWERTY du banc d'essai, puis un AZERTY qui en échange quelques
// lettres, sans lire SDL.

#include <cstdint>
#include <optional>
#include <string>

#include <doctest/doctest.h>

#include "levain/editor/shortcuts.hpp"
#include "levain/platform/input_script.hpp"

using namespace levain::editor;

namespace
{

constexpr std::uint16_t ScanA = 4, ScanQ = 20, ScanW = 26, ScanZ = 29, ScanF1 = 58;

/// Un AZERTY français pour ce qui change des lettres d'un QWERTY : A et Q, W et Z (le reste
/// est celui du QWERTY).
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

std::string displayOf(Action action, std::size_t chord, const levain::platform::KeyAtPosition& at)
{
    const ShortcutTable table = defaultShortcuts(); // `bindingOf` pointe dans la table : la garder
    const Binding* binding = bindingOf(table, action);
    REQUIRE(binding != nullptr);
    return displayNameOf(binding->chords.at(chord), at);
}

} // namespace

TEST_CASE("les défauts : chaque action une fois, au plus deux accords, le contexte de l'ADR")
{
    const ShortcutTable table = defaultShortcuts();
    CHECK(table.bindings.size() == static_cast<std::size_t>(Action::Count));
    for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(Action::Count); ++i)
    {
        const auto action = static_cast<Action>(i);
        const Binding* binding = bindingOf(table, action);
        REQUIRE_MESSAGE(binding != nullptr, nameOf(action));
        CHECK(isBound(binding->chords[0])); // la première place est toujours prise
        CHECK(std::string{nameOf(action)}.size() > 2);
    }
    CHECK(bindingOf(table, Action::MoveForward)->context == Context::Flight);
    CHECK(bindingOf(table, Action::FrameSelection)->context == Context::View);
    CHECK(bindingOf(table, Action::Save)->context == Context::Global);
    CHECK(bindingOf(table, Action::Stop)->context == Context::Game);
    CHECK(bindingOf(table, Action::GiveMouseBack)->context == Context::Game);
    // Rétablir a ses deux accords, et eux seuls.
    const Binding& redo = *bindingOf(table, Action::Redo);
    CHECK(redo.chords[0] == letterChord('y', {.ctrl = true}));
    CHECK(redo.chords[1] == letterChord('z', {.ctrl = true, .shift = true}));
    CHECK_FALSE(isBound(bindingOf(table, Action::Save)->chords[1]));
    // Le vol et les outils vont par position, les accords à Ctrl ou Alt par lettre.
    CHECK(bindingOf(table, Action::MoveForward)->chords[0].kind == ChordKind::Position);
    CHECK(bindingOf(table, Action::ToolMove)->chords[0].kind == ChordKind::Position);
    CHECK(bindingOf(table, Action::RealTime)->chords[0].kind == ChordKind::Letter);
    CHECK(bindingOf(table, Action::Play)->chords[0].kind == ChordKind::Letter);
    // Un accord du global ne doit pas être aussi celui d'un autre contexte (hors jeu, qui vaut
    // seul) : le vol ou la Vue le prendraient avant lui.
    for (const Binding& global : table.bindings)
    {
        for (const Binding& other : table.bindings)
        {
            if (global.context == Context::Global && other.context != Context::Global &&
                other.context != Context::Game)
            {
                for (const Chord& cg : global.chords)
                {
                    for (const Chord& co : other.chords)
                    {
                        CHECK_FALSE((isBound(cg) && cg == co));
                    }
                }
            }
        }
    }
    // Aucun accord n'est donné deux fois dans un même contexte : un défaut en conflit serait un
    // bug.
    for (const Binding& a : table.bindings)
    {
        for (const Binding& b : table.bindings)
        {
            if (&a != &b && a.context == b.context)
            {
                for (const Chord& ca : a.chords)
                {
                    for (const Chord& cb : b.chords)
                    {
                        CHECK_FALSE((isBound(ca) && ca == cb));
                    }
                }
            }
        }
    }
}

TEST_CASE("une paire AZERTY donne Z à la place du W : l'affichage suit la disposition")
{
    const auto qwerty = levain::platform::qwertyKeyAtPosition;
    CHECK(displayOf(Action::MoveForward, 0, qwerty) == "W");
    CHECK(displayOf(Action::MoveForward, 0, azertyAt) == "Z");
    CHECK(displayOf(Action::MoveLeft, 0, qwerty) == "A");
    CHECK(displayOf(Action::MoveLeft, 0, azertyAt) == "Q");
    CHECK(displayOf(Action::MoveDown, 0, azertyAt) == "A"); // E et A en AZERTY
    CHECK(displayOf(Action::MoveUp, 0, azertyAt) == "E");
    CHECK(displayOf(Action::ToolMove, 0, azertyAt) == "Z"); // Z, E, R
    CHECK(displayOf(Action::ToolRotate, 0, azertyAt) == "E");
    // Une lettre est la lettre, quelle que soit la disposition ; une touche qui ne tape rien a son
    // nom.
    CHECK(displayOf(Action::Undo, 0, azertyAt) == "Ctrl+Z");
    CHECK(displayOf(Action::Redo, 1, azertyAt) == "Ctrl+Maj+Z");
    CHECK(displayOf(Action::Play, 0, azertyAt) == "Alt+P");
    CHECK(displayOf(Action::NextTool, 0, azertyAt) == "Espace");
    CHECK(displayOf(Action::Stop, 0, azertyAt) == "Échap");
    CHECK(displayOf(Action::GiveMouseBack, 0, azertyAt) == "Maj+F1");
    // Une position que la disposition ne nomme pas : son nom stable, jamais une chaîne vide.
    CHECK(displayNameOf(positionChord(200), [](std::uint16_t) { return 0U; }) == "pos:200");
}

TEST_CASE("le nom stable d'un accord : la position par son scancode, la lettre par la sienne")
{
    CHECK(tokenOf(positionChord(ScanW)) == "pos:26");
    CHECK(tokenOf(letterChord('s', {.ctrl = true})) == "Ctrl+S");
    CHECK(tokenOf(positionChord(ScanF1, {.shift = true})) == "Shift+pos:58");
    CHECK(tokenOf(letterChord('z', {.ctrl = true, .shift = true, .alt = true, .super = true})) ==
          "Ctrl+Shift+Alt+Super+Z");
    // Aller-retour pour tous les défauts.
    for (const Binding& binding : defaultShortcuts().bindings)
    {
        for (const Chord& chord : binding.chords)
        {
            if (isBound(chord))
            {
                CHECK(chordFromToken(tokenOf(chord)) == chord);
            }
        }
    }
    // Un nom qui n'en est pas un est refusé : ni nom de SDL, ni nom d'ImGui, ni scancode absurde.
    for (const char* bad : {"", "Ctrl+", "ctrl+S", "SDL_SCANCODE_W", "ImGuiKey_W", "pos:", "pos:0",
                            "pos:99999", "pos:2x", "Ctrl+SS", "Ctrl+é"})
    {
        CHECK_FALSE(chordFromToken(bad).has_value());
    }
    // Ni un modificateur seul, ni F1 nue (les panneaux), ni une autre écriture du même accord :
    // le fichier relu puis réécrit ne doit pas changer.
    for (const char* bad : {"pos:224", "Ctrl+pos:225", "pos:231", "pos:58", "pos:026", "pos:+26",
                            "Shift+Ctrl+S", "Ctrl+Ctrl+S", "Ctrl+pos:058"})
    {
        CHECK_MESSAGE(!chordFromToken(bad).has_value(), bad);
    }
    CHECK(chordFromToken("Shift+pos:58") == positionChord(ScanF1, {.shift = true})); // Maj+F1
    CHECK(chordFromToken("pos:232").has_value()); // juste après les modificateurs
    // Pas d'accord : pas de nom (la seconde place d'une action qui n'a qu'un accord).
    CHECK(tokenOf(Chord{}).empty());
}
