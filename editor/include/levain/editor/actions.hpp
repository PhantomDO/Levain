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
#include <cstddef>
#include <cstdint>
#include <optional>

#include "levain/editor/mode.hpp"
#include "levain/editor/shortcuts.hpp"

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

} // namespace levain::editor
