#pragma once

// Les actions de l'éditeur (ADR-0036, décisions 12 et 13) : ce qu'un menu, une barre ou un
// raccourci atteint. **Une action grisée tant que son milestone n'est pas là** (`availabilityOf`) :
// le menu la montre éteinte et ne fait rien au clic, mais son raccourci l'atteint quand même, et
// elle le dit à la console (« Enregistrer : arrive en M7.3 ») au lieu de se taire : un Ctrl+S qui
// ne répond pas sans rien dire laisserait croire qu'une scène est enregistrée (règle n°7), et le
// compteur `declined` permet à un test de l'observer. La logique est en fonctions libres dont
// toutes les dépendances sont des paramètres (ADR-0011) ; `withEditor` n'en a que la glu.
// Équivalents : les `FUICommandInfo` et leur `FUICommandList` d'Unreal, les `MenuItem` d'Unity,
// les `EditorAction` de Godot.

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "levain/editor/mode.hpp"
#include "levain/editor/shortcuts.hpp"
#include "levain/input/state.hpp"
#include "levain/platform/window.hpp"

namespace levain::editor
{

/// Une action est-elle là, et sinon dans quel milestone elle arrive.
struct Availability
{
    bool available = true;
    const char* milestone = ""; ///< « M7.3 » : le texte tel que la console le dit ; vide si là.
};

/// **La disponibilité des actions** (décision 12) : Enregistrer, Annuler et Rétablir arrivent en
/// M7.3 (la scène sur disque et l'historique), les outils en M7.4, Simuler en M7.5. Le reste est
/// là : jouer, arrêter, le temps réel, et les actions de navigation (le morceau 11).
[[nodiscard]] constexpr Availability availabilityOf(Action action)
{
    switch (action)
    {
    case Action::Save:
    case Action::Undo:
    case Action::Redo:
        return {.available = false, .milestone = "M7.3"};
    case Action::ToolMove:
    case Action::ToolRotate:
    case Action::ToolScale:
    case Action::NextTool:
        return {.available = false, .milestone = "M7.4"};
    case Action::Simulate:
        return {.available = false, .milestone = "M7.5"};
    default:
        return {};
    }
}

/// Une touche d'outil : Déplacer, Tourner, Mettre à l'échelle, ou le suivant.
[[nodiscard]] constexpr bool isToolAction(Action action)
{
    return action == Action::ToolMove || action == Action::ToolRotate ||
           action == Action::ToolScale || action == Action::NextTool;
}

inline constexpr std::size_t ActionCount = static_cast<std::size_t>(Action::Count);

[[nodiscard]] constexpr std::size_t indexOf(Action action)
{
    return static_cast<std::size_t>(action);
}

/// Combien de fois chaque action a été atteinte, par un menu, une barre ou un raccourci : faite
/// (`performed`) ou grisée et dite à la console (`declined`).
struct ActionCounters
{
    std::array<int, ActionCount> performed{};
    std::array<int, ActionCount> declined{};
};

/// **Le temps réel ne se règle qu'en Édition.** Jouer le force à vrai (le renard s'anime, l'eau et
/// l'herbe bougent : un jeu gelé n'est pas un jeu) et garde le choix de l'Édition dans
/// `editChoice` ; revenir à l'Édition le rend. À appeler à chaque changement de mode (`from` →
/// `to`) ; rien ne change si le mode reste du même côté. Le piège évité : décocher en Édition puis
/// jouer, et jouer un monde figé.
void switchRealTime(Mode from, Mode to, bool& appRealTime, bool& editChoice);

/// Atteint une action. **Grisée** : le compteur `declined` monte et la console dit « {action} :
/// arrive en {milestone} » ; rien d'autre ne change. **Là** : `performed` monte et l'effet part :
/// « Temps réel » bascule `realTime` (`App::realTime`) **en Édition** ; ailleurs il est grisé
/// (`declined`, dit à la console « seulement en Édition »), Jouer et Arrêter rendent le mode où
/// passer (Jouer ne fait que jouer, Arrêter que revenir : `modeRequested`). Les autres actions (la
/// navigation) sont comptées et n'ont pas d'effet ici.
[[nodiscard]] std::optional<Mode> runAction(Action action, ActionCounters& counters, bool& realTime,
                                            Mode current);

/// Une touche d'outil (`isToolAction`) appuyée dans la Vue : l'action de l'outil, ou rien.
/// **En vol (clic droit tenu), aucune** : `actionFor` rend Avancer pour W et ne laisse rien à la
/// Vue pour R ou Espace (la Vue se tait en vol). Avec Ctrl ou tout autre modificateur, ce n'est pas
/// la touche d'outil non plus (`exactModifiers`), et dans un champ de texte actif, ni l'un ni
/// l'autre (`textInput`).
[[nodiscard]] std::optional<Action> toolActionOf(const ShortcutTable& table, const KeyPress& press,
                                                 const ActiveContexts& active);

/// **Les touches appuyées à cette image**, lues dans ses événements bruts (`App::rawFrameEvents`,
/// avant la route et la capture), dans leur ordre : la position (le scancode), la lettre que la
/// disposition y met (le keycode de l'événement) et les modificateurs tenus avec elle. Sous la
/// route *éditeur* le jeu n'a rien ; ImGui reçoit le clavier, mais traduit par lettre, et une
/// position se perdrait à repasser par lui. **Une répétition n'est pas un appui** : SDL répète une
/// touche tenue (`UiEventType::Key`, « répétitions comprises ») ; une touche déjà enfoncée (dans
/// `held`, les touches tenues au début de l'image, ou enfoncée plus tôt dans l'image) ne compte
/// qu'à son premier appui, sans quoi tenir W prendrait l'outil à chaque répétition. La fenêtre qui
/// perd le focus oublie les touches tenues : leurs relâchements ne viendront pas.
[[nodiscard]] std::vector<KeyPress> keyPressesOf(const platform::Events& events,
                                                 std::bitset<platform::KeyCodeCount> held);

/// **La Vue est « active »** quand les touches de la Vue valent (les outils, F, Ctrl+R) : panneaux
/// fermés, la scène remplit la fenêtre, toujours ; ouverts, la Vue doit avoir le focus ou être
/// survolée **au sens d'ImGui** (`View::hovered`) : une fenêtre, un menu ou un popup sous la
/// souris, posé par-dessus la Vue, la prive de la souris, et W ou Ctrl+R ne doivent pas partir à
/// travers eux. La règle des touches seulement : un geste de souris, lui, commence sous le
/// pointeur, le focus n'y compte pas (`GestureContext`).
[[nodiscard]] constexpr bool viewActiveIn(bool panelsOpen, bool focused, bool hovered)
{
    return !panelsOpen || focused || hovered;
}

} // namespace levain::editor
