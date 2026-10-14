// Changer la table des raccourcis (ADR-0036, décision 13, morceau 16) : de la logique pure. La
// disposition du clavier est un paramètre (le QWERTY du banc d'essai, puis un AZERTY qui échange W
// et Z, A et Q), la fenêtre n'est pas dessinée.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "levain/editor/preferences.hpp"
#include "levain/editor/shortcut_edit.hpp"
#include "levain/platform/input_script.hpp"

using namespace levain::editor;
using levain::platform::KeyModifiers;

namespace
{

constexpr std::uint16_t ScanA = 4, ScanQ = 20, ScanW = 26, ScanZ = 29, ScanS = 22, ScanEscape = 41,
                        ScanF1 = 58, ScanLeftCtrl = 224;
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

/// Pose un accord sans passer par `replaceChord` : de quoi fabriquer une table en conflit.
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

KeyPress letterPress(char letter, KeyModifiers modifiers = {})
{
    return {
        .scancode = ScanS, .keycode = static_cast<std::uint32_t>(letter), .modifiers = modifiers};
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

TEST_CASE("remplacer : l'accord passe à l'action, l'autre le perd, sa seconde place remonte")
{
    ShortcutTable table = defaultShortcuts();
    // Annuler prend Ctrl+Y, que Rétablir tenait en premier : Ctrl+Maj+Z remonte à sa première
    // place.
    const auto removed = replaceChord(table, Action::Undo, 1, letterChord('y', Ctrl), Qwerty);
    REQUIRE(removed.size() == 1);
    CHECK(removed.front().action == Action::Redo);
    CHECK(chordsOf(table, Action::Undo)[1] == letterChord('y', Ctrl));
    CHECK(chordsOf(table, Action::Undo)[0] == letterChord('z', Ctrl));
    CHECK(chordsOf(table, Action::Redo)[0] == letterChord('z', CtrlShift));
    CHECK_FALSE(isBound(chordsOf(table, Action::Redo)[1]));
    CHECK(conflictsIn(table, Qwerty).empty());
    // Ctrl+S à Annuler : Enregistrer n'a plus d'accord.
    replaceChord(table, Action::Undo, 0, letterChord('s', Ctrl), Qwerty);
    CHECK_FALSE(isBound(chordsOf(table, Action::Save)[0]));
    CHECK(chordsOf(table, Action::Undo)[0] == letterChord('s', Ctrl));
}

TEST_CASE(
    "remplacer : l'accord de sa propre autre place n'est pas un conflit, cette place est vidée")
{
    ShortcutTable table = defaultShortcuts();
    // Rétablir tient [Ctrl+Y, Ctrl+Maj+Z] : Ctrl+Y sur la seconde place n'est en conflit avec
    // personne, pas même avec Rétablir.
    CHECK(conflictsOf(table, Action::Redo, letterChord('y', Ctrl), Qwerty).empty());
    CHECK(replaceChord(table, Action::Redo, 1, letterChord('y', Ctrl), Qwerty).empty());
    // Ctrl+Y n'est plus qu'une fois, devant ; Ctrl+Maj+Z est perdu avec la place qu'il tenait.
    CHECK(chordsOf(table, Action::Redo)[0] == letterChord('y', Ctrl));
    CHECK_FALSE(isBound(chordsOf(table, Action::Redo)[1]));
    CHECK(conflictsIn(table, Qwerty).empty());
    // Un autre accord sur la seconde place ne touche pas à la première.
    table = defaultShortcuts();
    CHECK(replaceChord(table, Action::Redo, 1, letterChord('h', Ctrl), Qwerty).empty());
    CHECK(chordsOf(table, Action::Redo)[0] == letterChord('y', Ctrl));
    CHECK(chordsOf(table, Action::Redo)[1] == letterChord('h', Ctrl));
}

TEST_CASE("réinitialiser : une action, puis toutes, un défaut pris est repris à son nouveau maître")
{
    ShortcutTable table = defaultShortcuts();
    replaceChord(table, Action::Undo, 0, letterChord('s', Ctrl), Qwerty); // Enregistrer le perd
    CHECK_FALSE(isDefault(table, Action::Save));
    CHECK_FALSE(isDefault(table, Action::Undo));
    const auto removed = resetAction(table, Action::Save, Qwerty);
    REQUIRE(removed.size() == 1);
    CHECK(removed.front().action == Action::Undo);
    CHECK(isDefault(table, Action::Save));
    CHECK_FALSE(isBound(chordsOf(table, Action::Undo)[0])); // il n'est pas resté en conflit
    CHECK(conflictsIn(table, Qwerty).empty());
    resetAll(table);
    CHECK(linesOf(table).empty());
    CHECK(isDefault(table, Action::Undo));
}

TEST_CASE("saisie : F1 seule, un modificateur seul et Échap ne deviennent jamais un accord")
{
    const KeyPress f1{.scancode = ScanF1, .keycode = 0x4000003A, .modifiers = {}};
    CHECK(captureChord(f1, ChordKind::Position).outcome == CaptureOutcome::Reserved);
    CHECK(captureChord(f1, ChordKind::Letter).outcome == CaptureOutcome::Reserved);
    // Maj+F1 est « rendre la souris » : liable.
    KeyPress shiftF1 = f1;
    shiftF1.modifiers.shift = true;
    const auto shifted = captureChord(shiftF1, ChordKind::Position);
    CHECK(shifted.outcome == CaptureOutcome::Chord);
    CHECK(shifted.chord == positionChord(ScanF1, {.shift = true}));
    const KeyPress ctrl{.scancode = ScanLeftCtrl, .keycode = 0x400000E0, .modifiers = Ctrl};
    CHECK(captureChord(ctrl, ChordKind::Letter).outcome == CaptureOutcome::ModifierOnly);
    const KeyPress escape{.scancode = ScanEscape, .keycode = 0x1B, .modifiers = {}};
    CHECK(captureChord(escape, ChordKind::Position).outcome == CaptureOutcome::Cancel);
    // Alt-Gr tenu ne s'écrirait pas : refusé plutôt que d'être un accord qui ne part jamais.
    KeyPress altGr = letterPress('e');
    altGr.modifiers.altGr = true;
    CHECK(captureChord(altGr, ChordKind::Letter).outcome == CaptureOutcome::Unbindable);
    // Une flèche n'a pas de lettre : un accord de lettre la refuse.
    const KeyPress arrow{.scancode = 80, .keycode = 0x40000050, .modifiers = {}};
    CHECK(captureChord(arrow, ChordKind::Letter).outcome == CaptureOutcome::Unbindable);
}

TEST_CASE("saisie : le genre de l'accord suit celui de l'action")
{
    // Une position garde le scancode, même sur un AZERTY où la touche est un « z ».
    const KeyPress onW{.scancode = ScanW, .keycode = 'z', .modifiers = {}};
    const auto position = captureChord(onW, defaultKindOf(Action::ToolMove));
    CHECK(position.chord == positionChord(ScanW));
    // Une lettre garde le caractère de la touche, en minuscule.
    const auto letter = captureChord({.scancode = ScanW, .keycode = 'Z', .modifiers = Ctrl},
                                     defaultKindOf(Action::Save));
    CHECK(letter.chord == letterChord('z', Ctrl));
    CHECK(defaultKindOf(Action::MoveForward) == ChordKind::Position);
    CHECK(defaultKindOf(Action::Redo) == ChordKind::Letter);
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

TEST_CASE("les lignes : seules les actions changées s'écrivent, et se relisent à l'identique")
{
    ShortcutTable table = defaultShortcuts();
    CHECK(linesOf(table).empty());
    replaceChord(table, Action::Undo, 0, letterChord('s', Ctrl), Qwerty);
    replaceChord(table, Action::ToolMove, 0, positionChord(ScanZ), Qwerty);
    const std::vector<ShortcutLine> lines = linesOf(table);
    REQUIRE(lines.size() == 3); // Outil déplacer, Enregistrer (sans accord) et Annuler
    // Par le fichier : formaté, relu, appliqué.
    Preferences preferences;
    preferences.shortcuts = lines;
    const auto read = parsePreferences(formatPreferences(preferences));
    REQUIRE(read);
    const AppliedLines applied = shortcutsFromLines(read->shortcuts);
    CHECK(applied.problems.empty());
    CHECK(applied.unknown.empty());
    for (const Binding& binding : table.bindings)
    {
        CHECK(chordsOf(applied.table, binding.action) == binding.chords);
    }
    // Une action sans accord s'écrit sans rien après le « = ».
    CHECK(formatPreferences(preferences).contains("Enregistrer =\n"));
}

TEST_CASE("les lignes : une action inconnue est gardée, une ligne fautive ou double est dite")
{
    const AppliedLines applied = shortcutsFromLines({
        {.action = "Action d'une autre branche", .chords = {"Ctrl+Q"}},
        {.action = "Annuler", .chords = {"Ctrl+Q"}},
        {.action = "Annuler", .chords = {"Ctrl+W"}},
        {.action = "Enregistrer", .chords = {"pos:026"}},
        {.action = "Avancer", .chords = {"Ctrl+A", "Ctrl+B", "Ctrl+C"}},
    });
    REQUIRE(applied.unknown.size() == 1);
    CHECK(applied.unknown.front().action == "Action d'une autre branche");
    CHECK(applied.problems.size() == 3);
    CHECK(chordsOf(applied.table, Action::Undo)[0] == letterChord('q', Ctrl)); // la première ligne
    CHECK(isDefault(applied.table, Action::Save)); // « pos:026 » : plus d'un nom, défauts gardés
    CHECK(isDefault(applied.table, Action::MoveForward));
}

TEST_CASE("les lignes : un accord d'un autre genre que l'action est refusé et gardé tel quel")
{
    const ShortcutLine letterForPosition{.action = "Avancer", .chords = {"Z"}};
    const ShortcutLine positionForLetter{.action = "Enregistrer", .chords = {"Ctrl+pos:22"}};
    const ShortcutLine fine{.action = "Annuler", .chords = {"Ctrl+Q"}};
    const AppliedLines applied = shortcutsFromLines({letterForPosition, positionForLetter, fine});
    CHECK(applied.problems.size() == 2);
    CHECK(isDefault(applied.table, Action::MoveForward)); // le vol ne reste pas mort sans le dire
    CHECK(isDefault(applied.table, Action::Save));
    CHECK(chordsOf(applied.table, Action::Undo)[0] == letterChord('q', Ctrl));
    REQUIRE(applied.refused.size() == 2);
    CHECK(applied.refused.front().action == "Avancer");
    CHECK(applied.refused.back().chords == positionForLetter.chords);
    // Les genres justes passent : une position pour une action de position.
    const AppliedLines right = shortcutsFromLines({{.action = "Avancer", .chords = {"pos:29"}}});
    CHECK(right.problems.empty());
    CHECK(chordsOf(right.table, Action::MoveForward)[0] == positionChord(ScanZ));
    // L'écriture rend les lignes gardées, sauf celle d'une action changée depuis.
    ShortcutTable table = applied.table;
    replaceChord(table, Action::MoveForward, 0, positionChord(ScanQ), Qwerty);
    const auto written = withKeptLines(linesOf(table), applied.refused);
    std::size_t forward = 0, save = 0;
    for (const ShortcutLine& line : written)
    {
        forward += line.action == "Avancer" ? 1U : 0U;
        save += line.action == "Enregistrer" ? 1U : 0U;
    }
    CHECK(forward == 1);
    CHECK(save == 1);
    for (const ShortcutLine& line : written)
    {
        if (line.action == "Avancer")
        {
            CHECK(line.chords == std::vector<std::string>{tokenOf(positionChord(ScanQ))});
        }
        if (line.action == "Enregistrer")
        {
            CHECK(line.chords == positionForLetter.chords);
        }
    }
}
