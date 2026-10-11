#pragma once

// La table des raccourcis de l'éditeur (ADR-0036, décision 13), **les défauts seuls** : par action
// son nom (une clé du catalogue), au plus deux accords comme chez Unreal, un contexte. Le vol et
// les outils vont **par position** (le scancode : Z, E, R sur un AZERTY, aux places du W, E, R d'un
// QWERTY, et ils s'affichent ainsi) ; les accords à Ctrl ou Alt vont **par lettre**, pour que
// Ctrl+Z reste la touche marquée Z. Changer un accord, le garder et nommer un conflit sont les
// morceaux 15 et 16 ; l'action de chaque accord est branchée par les morceaux 11 et 12. La logique
// est en fonctions libres dont toutes les dépendances sont des paramètres (ADR-0011) : la
// disposition du clavier en est une (`platform::KeyAtPosition`), jamais lue ici. Équivalents : les
// « Editor Preferences > Keyboard Shortcuts » d'Unreal, le « Shortcuts Manager » d'Unity, l'«
// Editor Settings > Shortcuts » de Godot.

#include <array>
#include <cstdint>
#include <vector>

#include "levain/platform/input.hpp"

namespace levain::editor
{

enum class Action : std::uint8_t
{
    MoveForward,
    MoveBackward,
    MoveLeft,
    MoveRight,
    MoveUp,
    MoveDown,
    FrameSelection,
    GameView,
    RealTime,
    ToolMove,
    ToolRotate,
    ToolScale,
    NextTool,
    Save,
    Undo,
    Redo,
    Play,
    Simulate,
    Stop,
    GiveMouseBack,
    Count, ///< Pas une action : la taille de la table.
};

/// Où un accord vaut. **L'ordre est la précédence** (décision 13) : le vol (clic droit tenu) passe
/// avant la Vue (survolée ou en focus), qui passe avant le global ; le jeu ne vaut que sous la
/// route *jeu*, et seul.
enum class Context : std::uint8_t
{
    Flight,
    View,
    Global,
    Game,
};

/// Par position (le scancode, ce que lit le jeu) ou par lettre (le keycode, ce que lit ImGui).
enum class ChordKind : std::uint8_t
{
    Position,
    Letter,
};

/// Un accord : une touche et les modificateurs tenus avec elle. `key == 0` : pas d'accord (la
/// seconde place d'une action qui n'en a qu'un). Pour une lettre, `key` est le caractère en
/// minuscule (« s »), le keycode qu'ImGui reçoit.
struct Chord
{
    ChordKind kind = ChordKind::Position;
    std::uint32_t key = 0;
    platform::KeyModifiers modifiers{};

    bool operator==(const Chord&) const = default;
};

[[nodiscard]] constexpr bool isBound(const Chord& chord)
{
    return chord.key != 0;
}

/// Une action, son contexte et ses deux accords au plus (« Rétablir » : Ctrl+Y et Ctrl+Maj+Z).
struct Binding
{
    Action action = Action::Count;
    Context context = Context::Global;
    std::array<Chord, 2> chords{};
};

struct ShortcutTable
{
    std::vector<Binding> bindings;
};

/// Le nom de l'action, clé du catalogue (`ui::tr`) : le même texte français dans la table, la
/// fenêtre Aide et les préférences.
[[nodiscard]] const char* nameOf(Action action);

/// Les défauts de la décision 13 : les raccourcis d'Unreal [E1, E2] là où ils existent, ceux de
/// Godot pour Ctrl+Maj+Z, les nôtres pour le reste (la colonne « Source » de l'ADR).
[[nodiscard]] ShortcutTable defaultShortcuts();

[[nodiscard]] const Binding* bindingOf(const ShortcutTable& table, Action action);

/// Un accord de position : le scancode d'USB (le même partout, `platform` le vérifie contre SDL).
[[nodiscard]] constexpr Chord positionChord(std::uint16_t scancode,
                                            platform::KeyModifiers modifiers = {})
{
    return {.kind = ChordKind::Position, .key = scancode, .modifiers = modifiers};
}

/// Un accord de lettre : le caractère de la touche, en minuscule.
[[nodiscard]] constexpr Chord letterChord(char letter, platform::KeyModifiers modifiers = {})
{
    return {.kind = ChordKind::Letter,
            .key = static_cast<std::uint32_t>(letter),
            .modifiers = modifiers};
}

} // namespace levain::editor
