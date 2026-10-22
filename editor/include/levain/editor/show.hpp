#pragma once

// Le menu « Afficher » de la Vue (ADR-0036, décisions 13 et 15) : ce que la Vue montre en plus de
// la scène, « Grille » et « Vue de jeu » (G). **La vue de jeu cache les aides de l'éditeur** : la
// grille, et le contour de la sélection du programme (`App::editorHelpersVisible`). Jouer les
// cache aussi, comme le viewport de PIE d'Unreal. L'état est celui de la session : il n'est pas
// enregistré avec les préférences (editor/README.md, « Pas encore »). Équivalents : le menu « Show
// » du viewport d'Unreal et la touche G, « Gizmos » de la Scene View d'Unity, View > Gizmos de
// Godot.

#include <string>

#include <imgui.h>

#include "levain/app/ui_layer.hpp"
#include "levain/editor/mode.hpp"
#include "levain/editor/shortcuts.hpp"

namespace levain::editor
{

/// Ce que la Vue montre de plus que la scène.
struct ShowState
{
    bool grid = true;
    bool gameView = false;
};

/// Où le menu a été dessiné à la dernière image (le centre de chaque contrôle, en pixels de
/// l'écran), pour que les tests le cliquent à la souris scriptée.
struct ShowMenuLayout
{
    ImVec2 button{};
    ImVec2 gridItem{};     ///< valable `open`
    ImVec2 gameViewItem{}; ///< valable `open`
    bool open = false;     ///< le menu est ouvert à la dernière image
    bool hovered = false;  ///< la souris est sur la barre « Afficher »
    /// Le rectangle de la barre « Afficher » à l'écran, nul tant qu'elle n'est pas dessinée : ce
    /// qu'un test de pixels de la scène laisse de côté (le texte d'ImGui n'est pas stable).
    app::ScreenRect bar{};
};

/// Les aides de l'éditeur se voient : sous la caméra de l'éditeur (Édition, Simuler), hors vue de
/// jeu. Jouer est la vue de jeu.
[[nodiscard]] constexpr bool helpersVisibleIn(Mode mode, bool gameView)
{
    return editorOwnsViewIn(mode) && !gameView;
}

/// La grille se dessine : elle est demandée, et les aides se voient.
[[nodiscard]] constexpr bool gridVisibleIn(const ShowState& show, bool helpersVisible)
{
    return show.grid && helpersVisible;
}

/// L'effet d'une action sur ce que la Vue montre : « Vue de jeu » bascule. Les autres n'y touchent
/// pas ; vrai si l'état a changé.
bool applyShowAction(ShowState& show, Action action);

/// Dessine la barre « Afficher » dans la fenêtre de la Vue, à `corner` (en pixels de l'écran), et
/// son menu. « Grille » bascule `show.grid` ; « Vue de jeu », avec son raccourci `gameViewShortcut`
/// (celui de la table, selon le clavier), rend vrai : l'appelant atteint alors l'action, comme le
/// ferait la touche.
[[nodiscard]] bool drawShowMenu(ShowState& show, ImVec2 corner, const std::string& gameViewShortcut,
                                ShowMenuLayout& layout);

} // namespace levain::editor
