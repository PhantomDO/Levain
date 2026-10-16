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
#include <bitset>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

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
    Pause,     ///< Pause / Reprendre, en jeu (la touche Pause, ADR-0037, décision 6).
    NextFrame, ///< « Image suivante » : un pas à l'arrêt. **Sans accord par défaut** (voir
               ///< `defaultShortcuts`).
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
/// Godot pour Ctrl+Maj+Z, les nôtres pour le reste (la colonne « Source » de l'ADR). **« Image
/// suivante » n'a aucun accord** : Unreal n'en documente pas, et Ctrl+Alt+P d'Unity est AltGr+P
/// sous Windows en AZERTY ; la fenêtre « Raccourcis » peut lui en donner un.
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

/// **Le nom stable d'un accord**, celui qu'écrira le fichier des préférences (morceaux 15 et 16) :
/// les modificateurs dans l'ordre « Ctrl+Shift+Alt+Super+ », puis `pos:<scancode>` pour une
/// position (le numéro d'USB) ou la lettre en majuscule pour une lettre. Jamais un nom de SDL ou
/// d'ImGui, qui changent d'une version à l'autre (SDL_keyboard.h:275-282 ; imgui.h:1102). Le texte
/// vide pour « pas d'accord » (`Chord{}`) : la seconde place d'une action qui n'en a qu'un.
[[nodiscard]] std::string tokenOf(const Chord& chord);

/// Le contraire de `tokenOf`. Rien si le texte n'est pas un nom d'accord (un fichier mal écrit
/// échoue au chargement, règle n°7), ni s'il en est un que le moteur se réserve : une position de
/// modificateur seul (224 à 231, Ctrl ou Maj ne se lie pas), F1 sans modificateur (les panneaux,
/// `togglesPanels`). **Un seul nom par accord** : « pos:026 » ou des modificateurs dans le
/// désordre sont refusés, pour qu'un fichier relu et réécrit ne change pas.
[[nodiscard]] std::optional<Chord> chordFromToken(std::string_view token);

/// **Le nom qu'on affiche**, selon la disposition d'aujourd'hui : un accord de position montre la
/// touche que `keyAtPosition` met à cette place (« Z » pour le W d'un QWERTY, sur un AZERTY).
/// Les clés « Espace » et « Échap » sont celles du catalogue.
[[nodiscard]] std::string displayNameOf(const Chord& chord,
                                        const platform::KeyAtPosition& keyAtPosition);

/// Une touche qui vient d'être appuyée, avec les modificateurs tenus.
struct KeyPress
{
    std::uint16_t scancode = 0;
    std::uint32_t keycode = 0; ///< La lettre selon la disposition, comme `UiEvent::keycode`.
    platform::KeyModifiers modifiers{};
};

/// Les modificateurs tenus sont **exactement** ceux de l'accord : Ctrl+W n'est pas la touche
/// d'outil W (décision 13), et Alt-Gr tenu ne vaut jamais (AltGr+E d'un AZERTY tape « € », ce n'est
/// pas la touche E nue qui tournerait l'outil).
[[nodiscard]] constexpr bool exactModifiers(const platform::KeyModifiers& wanted,
                                            const platform::KeyModifiers& held)
{
    return !held.altGr && wanted == held;
}

/// L'appui est cet accord, **modificateurs exacts** (`exactModifiers`). Une lettre se compare en
/// minuscule.
[[nodiscard]] bool chordMatches(const Chord& chord, const KeyPress& press);

/// Les contextes actifs à cette image. Le global l'est toujours.
struct ActiveContexts
{
    bool flight = false; ///< Le vol est en cours (clic droit tenu, parti de la Vue : `inFlight`).
    bool view = false;   ///< Les touches de la Vue valent : survolée ou focalisée (`viewActiveIn`).
    bool game = false;   ///< La route est *jeu* : seul le contexte *jeu* vaut alors.
    /// Un champ de texte est actif (`io.WantTextInput`) : taper « w » dedans n'est pas l'outil
    /// Déplacer. Ni le vol, ni la Vue, ni le global ne valent alors ; le contexte *jeu* n'a pas
    /// de champ de texte, il n'est pas touché.
    bool textInput = false;
};

/// L'action qu'un appui déclenche, ou rien. **La précédence** : sous la route *jeu*, le contexte
/// *jeu* seul ; sinon le vol, puis la Vue, puis le global, le premier contexte qui reconnaît
/// l'appui l'emporte (clic droit tenu, W avance et ne prend pas l'outil Déplacer).
/// **En vol, la Vue se tait** : une touche de position de la Vue que le vol n'a pas (R, Espace,
/// F, G) ne fait rien tant que le clic droit est tenu. Le piège évité : R ou Espace qui
/// changeraient d'outil au milieu d'un vol, F qui recadrerait sous la souris capturée ; Unreal
/// réserve le clavier au vol pendant le clic droit (supposé). Le global vaut toujours (Ctrl+S).
///
/// **Le contrat avec ImGui** : `actionFor` sert les accords de **position** (le vol, les outils)
/// et le contexte *jeu*. Les accords de **lettre** (Ctrl+S, Ctrl+Z, Alt+P...) passent par
/// `ImGui::Shortcut(imguiChordOf(...))`, qui connaît le focus et les champs de texte : les
/// demander aussi à `actionFor` les ferait partir deux fois. Le champ de texte actif se dit par
/// `ActiveContexts::textInput` (`io.WantTextInput`).
[[nodiscard]] std::optional<Action> actionFor(const ShortcutTable& table, const KeyPress& press,
                                              const ActiveContexts& active);

/// **Une action tenue** : le jeu la lit comme une touche enfoncée (`positionHeld`), jamais comme un
/// appui à modificateurs exacts. C'est le vol (Avancer, Reculer, Gauche, Droite, Monter, Descendre,
/// que Maj tenu accélère sans les couper) et « Arrêter » (`stopPressed`). Un accord à modificateur
/// sur l'une d'elles serait affiché et enregistré sans jamais être lu comme tel : Maj+E sur
/// « Avancer » prendrait la touche E à « Monter » sans que rien ne le dise (invariant 22). La
/// saisie retire donc les modificateurs (`captureChord`), et le fichier qui en porte est refusé
/// (`shortcutsFromLines`).
[[nodiscard]] constexpr bool isHeldAction(Action action)
{
    switch (action)
    {
    case Action::MoveForward:
    case Action::MoveBackward:
    case Action::MoveLeft:
    case Action::MoveRight:
    case Action::MoveUp:
    case Action::MoveDown:
    case Action::Stop:
        return true;
    default:
        return false;
    }
}

/// Une touche de position de l'action vient d'être appuyée (`scancode`), **modificateurs ignorés**
/// : le front de ce que `positionHeld` lit à l'état tenu, pour « Arrêter » (`isHeldAction`).
[[nodiscard]] bool positionIs(const Binding& binding, std::uint16_t scancode);

/// Une touche de position de l'action est tenue, **modificateurs ignorés** : Maj tenu accélère le
/// vol sans le couper (`isHeldAction` : les actions qui se lisent ainsi). Les accords de lettre ne
/// comptent pas ici (ImGui les décide).
[[nodiscard]] bool positionHeld(const Binding& binding,
                                const std::bitset<platform::KeyCodeCount>& keys);

/// La touche d'ImGui d'un accord de lettre, pour `ImGui::Shortcut` (route globale pour Fichier, du
/// focus pour les outils) ; rien pour un accord de position. **ImGui reste traduit par keycode** :
/// Ctrl+Z est la touche marquée Z.
[[nodiscard]] std::optional<ImGuiKeyChord> imguiChordOf(const Chord& chord);

/// **Un accord de lettre de l'action vient d'être appuyé**, selon ImGui (`ImGui::Shortcut` sur
/// chaque accord de `imguiChordOf`, en route globale : quelle que soit la fenêtre qui a le focus).
/// Un champ de texte actif passe avant les routes (imgui.h, `ImGuiInputFlags_RouteGlobal`) : il
/// garde son Ctrl+Z, et Alt+P ne joue pas pendant qu'on tape. Les accords de position ne comptent
/// pas ici (`actionFor` les sert). « Rétablir » en a deux. À appeler dans une image d'ImGui.
[[nodiscard]] bool letterChordPressed(const Binding& binding);

} // namespace levain::editor
