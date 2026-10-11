#include "levain/editor/shortcuts.hpp"

#include <algorithm>
#include <cstddef>

namespace levain::editor
{

namespace
{

// Les scancodes d'USB HID des défauts : ils sont les mêmes partout.
constexpr std::uint16_t ScanA = 4, ScanD = 7, ScanE = 8, ScanF = 9, ScanG = 10, ScanQ = 20,
                        ScanR = 21, ScanS = 22, ScanW = 26, ScanEscape = 41, ScanSpace = 44,
                        ScanF1 = 58;

constexpr platform::KeyModifiers Ctrl{.ctrl = true};
constexpr platform::KeyModifiers Shift{.shift = true};
constexpr platform::KeyModifiers Alt{.alt = true};
constexpr platform::KeyModifiers CtrlShift{.ctrl = true, .shift = true};

Binding bind(Action action, Context context, Chord first, Chord second = {})
{
    return {.action = action, .context = context, .chords = {first, second}};
}

} // namespace

const char* nameOf(Action action)
{
    switch (action)
    {
    case Action::MoveForward:
        return "Avancer";
    case Action::MoveBackward:
        return "Reculer";
    case Action::MoveLeft:
        return "Aller à gauche";
    case Action::MoveRight:
        return "Aller à droite";
    case Action::MoveUp:
        return "Monter";
    case Action::MoveDown:
        return "Descendre";
    case Action::FrameSelection:
        return "Cadrer la sélection";
    case Action::GameView:
        return "Vue de jeu";
    case Action::RealTime:
        return "Temps réel";
    case Action::ToolMove:
        return "Outil déplacer";
    case Action::ToolRotate:
        return "Outil tourner";
    case Action::ToolScale:
        return "Outil mettre à l'échelle";
    case Action::NextTool:
        return "Outil suivant";
    case Action::Save:
        return "Enregistrer";
    case Action::Undo:
        return "Annuler";
    case Action::Redo:
        return "Rétablir";
    case Action::Play:
        return "Jouer";
    case Action::Simulate:
        return "Simuler";
    case Action::Stop:
        return "Arrêter";
    case Action::GiveMouseBack:
        return "Rendre la souris";
    case Action::Count:
        break;
    }
    return "";
}

ShortcutTable defaultShortcuts()
{
    using enum Action;
    using enum Context;
    return {
        .bindings = {
            // Le vol : W, S, A, D et E, Q à la place des touches d'un QWERTY (Z, S, Q, D et E, A
            // sur un AZERTY), comme Unreal et Unity.
            bind(MoveForward, Flight, positionChord(ScanW)),
            bind(MoveBackward, Flight, positionChord(ScanS)),
            bind(MoveLeft, Flight, positionChord(ScanA)),
            bind(MoveRight, Flight, positionChord(ScanD)),
            bind(MoveUp, Flight, positionChord(ScanE)),
            bind(MoveDown, Flight, positionChord(ScanQ)),
            // La Vue : F cadre, G bascule la vue de jeu, W E R les outils (M7.4), Espace le
            // suivant. Temps réel est un accord à Ctrl, donc par lettre.
            bind(FrameSelection, View, positionChord(ScanF)),
            bind(GameView, View, positionChord(ScanG)),
            bind(RealTime, View, letterChord('r', Ctrl)),
            bind(ToolMove, View, positionChord(ScanW)),
            bind(ToolRotate, View, positionChord(ScanE)),
            bind(ToolScale, View, positionChord(ScanR)),
            bind(NextTool, View, positionChord(ScanSpace)),
            // Le global : par lettre, comme les menus de tous les logiciels.
            bind(Save, Global, letterChord('s', Ctrl)),
            bind(Undo, Global, letterChord('z', Ctrl)),
            bind(Redo, Global, letterChord('y', Ctrl), letterChord('z', CtrlShift)),
            bind(Play, Global, letterChord('p', Alt)),
            bind(Simulate, Global, letterChord('s', Alt)),
            // Le jeu : Échap arrête, Maj+F1 rend la souris (F1 seule ouvre les panneaux).
            bind(Stop, Game, positionChord(ScanEscape)),
            bind(GiveMouseBack, Game, positionChord(ScanF1, Shift)),
        }};
}

const Binding* bindingOf(const ShortcutTable& table, Action action)
{
    const auto found = std::ranges::find(table.bindings, action, &Binding::action);
    return found == table.bindings.end() ? nullptr : &*found;
}

} // namespace levain::editor
