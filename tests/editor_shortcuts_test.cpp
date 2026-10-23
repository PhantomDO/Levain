// La table des raccourcis (ADR-0036, décision 13, morceau 10) : de la logique pure. La disposition
// du clavier est un paramètre : le QWERTY du banc d'essai, puis un AZERTY qui en échange quelques
// lettres, sans lire SDL.

#include <bitset>
#include <cstdint>
#include <optional>
#include <string>

#include <doctest/doctest.h>

#include "levain/editor/shortcut_edit.hpp"
#include "levain/editor/shortcuts.hpp"
#include "levain/platform/input_script.hpp"

using namespace levain::editor;
using levain::platform::KeyModifiers;

namespace
{

constexpr std::uint16_t ScanA = 4, ScanQ = 20, ScanW = 26, ScanZ = 29, ScanS = 22, ScanE = 8,
                        ScanR = 21, ScanF = 9, ScanF1 = 58, ScanEscape = 41, ScanSpace = 44;

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

KeyPress pressOf(std::uint16_t scancode, const levain::platform::KeyAtPosition& at,
                 KeyModifiers modifiers = {})
{
    return {.scancode = scancode, .keycode = at(scancode), .modifiers = modifiers};
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
        // La première place est toujours prise, sauf « Image suivante » : Unreal n'en documente
        // pas, et Ctrl+Alt+P d'Unity est AltGr+P en AZERTY (ADR-0037, décision 6).
        CHECK(isBound(binding->chords[0]) == (action != Action::NextFrame));
        CHECK(std::string{nameOf(action)}.size() > 2);
    }
    // Pause : la touche Pause (USB 72), du contexte jeu comme Échap ; Image suivante sans accord.
    CHECK(bindingOf(table, Action::Pause)->context == Context::Game);
    CHECK(bindingOf(table, Action::Pause)->chords[0] == positionChord(72));
    CHECK(bindingOf(table, Action::NextFrame)->context == Context::Game);
    CHECK_FALSE(isBound(bindingOf(table, Action::NextFrame)->chords[1]));
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
    // Les touches sans lettre que la saisie lie à une position ont un nom, pas « pos:82 ».
    constexpr std::uint32_t Mask = 1U << 30U;
    const auto nameAt = [](std::uint32_t keycode)
    {
        return displayNameOf(positionChord(82, {.ctrl = true}),
                             [keycode](std::uint16_t) { return keycode; });
    };
    CHECK(nameAt(Mask | 82U) == "Ctrl+Haut");
    CHECK(nameAt(Mask | 81U) == "Ctrl+Bas");
    CHECK(nameAt(Mask | 80U) == "Ctrl+Gauche");
    CHECK(nameAt(Mask | 79U) == "Ctrl+Droite");
    CHECK(nameAt('\r') == "Ctrl+Entrée");
    CHECK(nameAt('\t') == "Ctrl+Tab");
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

TEST_CASE("un appui trouve son accord : modificateurs égaux, position ou lettre")
{
    const auto at = azertyAt;
    // La touche à la place du W (lettre Z) : par position, le Z d'un AZERTY est le W d'un QWERTY.
    CHECK(chordMatches(positionChord(ScanW), pressOf(ScanW, at)));
    CHECK_FALSE(chordMatches(positionChord(ScanW), pressOf(ScanZ, at)));
    // Ctrl+W n'est pas la touche d'outil W (`exactModifiers`).
    CHECK_FALSE(chordMatches(positionChord(ScanW), pressOf(ScanW, at, {.ctrl = true})));
    // Une lettre se lit par keycode : Ctrl+Z est la touche qui tape Z, à la place du W en AZERTY.
    const Chord undo = letterChord('z', {.ctrl = true});
    CHECK(chordMatches(undo, pressOf(ScanW, at, {.ctrl = true})));
    CHECK_FALSE(chordMatches(undo, pressOf(ScanZ, at, {.ctrl = true})));
    CHECK_FALSE(chordMatches(undo, pressOf(ScanW, at, {.ctrl = true, .shift = true})));
    CHECK_FALSE(chordMatches(Chord{}, KeyPress{})); // pas d'accord : rien ne le reconnaît
}

TEST_CASE("Alt-Gr tenu ne vaut aucun accord : AltGr+E d'un AZERTY tape « € », pas la touche E")
{
    const auto qwerty = levain::platform::qwertyKeyAtPosition;
    const KeyModifiers altGr{.altGr = true};
    CHECK(chordMatches(positionChord(ScanE), pressOf(ScanE, qwerty))); // sans lui, la touche vaut
    CHECK_FALSE(chordMatches(positionChord(ScanE), pressOf(ScanE, qwerty, altGr)));
    CHECK_FALSE(chordMatches(letterChord('p', {.alt = true}),
                             pressOf(ScanE, qwerty, {.alt = true, .altGr = true})));
    // Et par la table entière : la touche E nue tourne, avec Alt-Gr elle ne fait rien.
    const ShortcutTable table = defaultShortcuts();
    CHECK_FALSE(actionFor(table, pressOf(ScanE, qwerty, altGr), {.view = true}).has_value());
    CHECK(actionFor(table, pressOf(ScanE, qwerty), {.view = true}) == Action::ToolRotate);
}

TEST_CASE("la précédence : le vol avant la Vue avant le global, le jeu seul sous la route jeu, la "
          "Vue muette en vol")
{
    const ShortcutTable table = defaultShortcuts();
    const auto at = azertyAt;
    const KeyPress w = pressOf(ScanW, at); // la touche Z d'un AZERTY
    // Clic droit tenu dans la Vue : la touche vole, elle ne prend pas l'outil Déplacer.
    CHECK(actionFor(table, w, {.flight = true, .view = true}) == Action::MoveForward);
    CHECK(actionFor(table, w, {.view = true}) == Action::ToolMove);
    CHECK_FALSE(actionFor(table, w, {}).has_value()); // hors de la Vue : rien
    // Le global vaut partout, le vol compris.
    const KeyPress save = pressOf(ScanS, levain::platform::qwertyKeyAtPosition, {.ctrl = true});
    CHECK(actionFor(table, save, {}) == Action::Save);
    CHECK(actionFor(table, save, {.flight = true, .view = true}) == Action::Save);
    // Ctrl+W (Ctrl avec la touche d'outil) ne prend aucun outil ; en AZERTY, la même position est
    // Ctrl+Z, qui annule : la lettre l'emporte pour les accords à Ctrl.
    const auto qwerty = levain::platform::qwertyKeyAtPosition;
    CHECK_FALSE(actionFor(table, pressOf(ScanW, qwerty, {.ctrl = true}), {.view = true}));
    CHECK(actionFor(table, pressOf(ScanW, at, {.ctrl = true}), {.view = true}) == Action::Undo);
    // Sous la route jeu : Échap arrête, Maj+F1 rend la souris, Ctrl+S ne part pas, W ne vole pas.
    const ActiveContexts game{.flight = true, .view = true, .game = true};
    CHECK(actionFor(table, pressOf(ScanEscape, at), game) == Action::Stop);
    CHECK(actionFor(table, pressOf(ScanF1, at, {.shift = true}), game) == Action::GiveMouseBack);
    CHECK_FALSE(actionFor(table, save, game).has_value());
    CHECK_FALSE(actionFor(table, w, game).has_value());
    // Le jeu n'a pas Échap hors de la route jeu (la table la réserve à ce contexte).
    CHECK_FALSE(actionFor(table, pressOf(ScanEscape, at), {.view = true}).has_value());
    // F1 seule n'est à aucune action : c'est la touche des panneaux, réservée.
    CHECK_FALSE(actionFor(table, pressOf(ScanF1, at), game).has_value());
    CHECK(actionFor(table, pressOf(ScanSpace, at), {.view = true}) == Action::NextTool);
    // En vol, la Vue se tait : ni R, ni Espace, ni F ne prennent un outil ou ne cadrent.
    const ActiveContexts flight{.flight = true, .view = true};
    CHECK_FALSE(actionFor(table, pressOf(ScanR, at), flight).has_value());
    CHECK_FALSE(actionFor(table, pressOf(ScanSpace, at), flight).has_value());
    CHECK_FALSE(actionFor(table, pressOf(ScanF, at), flight).has_value());
    CHECK(actionFor(table, pressOf(ScanR, at), {.view = true}) == Action::ToolScale);
}

TEST_CASE("un champ de texte actif ne laisse rien au vol, à la Vue ni au global")
{
    const ShortcutTable table = defaultShortcuts();
    const auto at = azertyAt;
    const KeyPress w = pressOf(ScanW, at); // taper « z » dans un champ n'est pas l'outil Déplacer
    CHECK(actionFor(table, w, {.view = true}) == Action::ToolMove);
    CHECK_FALSE(actionFor(table, w, {.view = true, .textInput = true}).has_value());
    CHECK_FALSE(actionFor(table, w, {.flight = true, .view = true, .textInput = true}).has_value());
    const KeyPress save = pressOf(ScanS, levain::platform::qwertyKeyAtPosition, {.ctrl = true});
    CHECK(actionFor(table, save, {}) == Action::Save);
    CHECK_FALSE(actionFor(table, save, {.textInput = true}).has_value());
    // Le jeu n'a pas de champ de texte : sa précédence ne bouge pas.
    CHECK(actionFor(table, pressOf(ScanEscape, at), {.game = true, .textInput = true}) ==
          Action::Stop);
}

TEST_CASE("une touche de position tenue : Maj ne coupe pas le vol, une lettre ne se lit pas ainsi")
{
    const ShortcutTable table = defaultShortcuts();
    std::bitset<levain::platform::KeyCodeCount> keys;
    CHECK_FALSE(positionHeld(*bindingOf(table, Action::MoveForward), keys));
    keys.set(ScanW);
    CHECK(positionHeld(*bindingOf(table, Action::MoveForward), keys));
    CHECK_FALSE(positionHeld(*bindingOf(table, Action::MoveBackward), keys));
    keys.set(ScanE);
    CHECK(positionHeld(*bindingOf(table, Action::MoveUp), keys));
    CHECK_FALSE(positionHeld(*bindingOf(table, Action::Save), keys)); // un accord de lettre
}

TEST_CASE("un accord de lettre pour ImGui::Shortcut : Ctrl+Z, par la touche marquée Z")
{
    const auto chord = imguiChordOf(letterChord('z', {.ctrl = true, .shift = true}));
    REQUIRE(chord.has_value());
    const auto bits = static_cast<unsigned>(chord ? *chord : 0);
    CHECK((bits & static_cast<unsigned>(ImGuiMod_Ctrl)) != 0U);
    CHECK((bits & static_cast<unsigned>(ImGuiMod_Shift)) != 0U);
    CHECK((bits & static_cast<unsigned>(ImGuiMod_Alt)) == 0U);
    CHECK((bits & ~static_cast<unsigned>(ImGuiMod_Mask_)) == static_cast<unsigned>(ImGuiKey_Z));
    CHECK_FALSE(
        imguiChordOf(positionChord(ScanW)).has_value()); // une position n'est pas pour ImGui
    CHECK_FALSE(imguiChordOf(Chord{}).has_value());
}

TEST_CASE("Suppr et F2 sont du contexte de la sélection, Créer une entité du global : noms, "
          "précédence, conflit avec la Vue")
{
    constexpr std::uint16_t ScanDelete = 76, ScanF2 = 59;
    const ShortcutTable table = defaultShortcuts();
    CHECK(bindingOf(table, Action::DeleteEntity)->context == Context::Scene);
    CHECK(bindingOf(table, Action::DeleteEntity)->chords[0] == positionChord(ScanDelete));
    CHECK(bindingOf(table, Action::RenameEntity)->chords[0] == positionChord(ScanF2));
    CHECK(bindingOf(table, Action::CreateEntity)->context == Context::Global);
    CHECK(bindingOf(table, Action::CreateEntity)->chords[0] ==
          letterChord('n', {.ctrl = true, .shift = true}));
    const auto qwerty = levain::platform::qwertyKeyAtPosition;
    CHECK(displayOf(Action::DeleteEntity, 0, qwerty) == "Suppr");
    CHECK(displayOf(Action::RenameEntity, 0, qwerty) == "F2");
    CHECK(displayOf(Action::CreateEntity, 0, qwerty) == "Ctrl+Maj+N");

    // La sélection vaut quand elle est active, sauf en vol et dans un champ de texte.
    const KeyPress del = pressOf(ScanDelete, qwerty);
    CHECK(actionFor(table, del, {.scene = true}) == Action::DeleteEntity);
    CHECK_FALSE(actionFor(table, del, {}).has_value());
    CHECK_FALSE(actionFor(table, del, {.flight = true, .scene = true}).has_value());
    CHECK_FALSE(actionFor(table, del, {.scene = true, .textInput = true}).has_value());
    CHECK_FALSE(actionFor(table, del, {.scene = true, .game = true}).has_value());
    // La Vue focalisée rend la Vue et la sélection actives ensemble : elles se disputent un accord.
    CHECK(contextsOverlap(Context::Scene, Context::View));
    CHECK(contextsOverlap(Context::View, Context::Scene));
    CHECK(contextsOverlap(Context::Scene, Context::Global));
    CHECK_FALSE(contextsOverlap(Context::Scene, Context::Flight));
    CHECK_FALSE(contextsOverlap(Context::Scene, Context::Game));
}
