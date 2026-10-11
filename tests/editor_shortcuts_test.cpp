// La table des raccourcis (ADR-0036, décision 13, morceau 10) : de la logique pure.

#include <cstdint>
#include <string>

#include <doctest/doctest.h>

#include "levain/editor/shortcuts.hpp"

using namespace levain::editor;

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
