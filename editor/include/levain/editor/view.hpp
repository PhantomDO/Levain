#pragma once

// La Vue de l'éditeur (ADR-0036, décision 7) : la fenêtre qui montre la scène rendue dans une
// texture (`App::requestScene`, morceau 7). Elle demande sa taille de contenu à chaque image, et 0
// × 0 à chaque image où elle n'est pas montrée (fermée, repliée, derrière un onglet) : « rien
// demandé » est la scène dans la fenêtre, que la Vue ne doit jamais provoquer. Elle publie aussi le
// rectangle de son image (`App::sceneRect`), que la route de l'input et le picking du sandbox
// lisent. Équivalents : la Scene View d'Unity, le Viewport d'Unreal, le Viewport 3D de Godot.

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

#include <imgui.h>

#include "levain/app/app.hpp"
#include "levain/editor/mode.hpp"
#include "levain/editor/show.hpp"

namespace levain::editor
{

/// La clé de catalogue du titre de la fenêtre.
inline constexpr const char* ViewWindow = "Vue";

/// Ce que la Vue garde d'une image à l'autre, et ce qu'elle a fait à la dernière.
struct View
{
    bool open = true; ///< Le drapeau que la croix et le menu « Fenêtre » partagent.
    /// Mettre la Vue en avant à sa prochaine image : Play lui donne le focus, ainsi le jeu reçoit
    /// le clavier sans clic préalable (la route *jeu* demande la Vue focalisée).
    bool focusRequested = false;
    bool shown = false; ///< `ImGui::Begin` a rendu vrai à la dernière image : l'onglet se voit.
    /// La Vue a le focus à la dernière image, sa barre de mode comprise : cliquer le bouton de la
    /// barre n'en retire pas le focus (`ImGuiFocusedFlags_ChildWindows`).
    bool focused = false;
    /// La souris est sur la barre de mode à la dernière image : la route de l'image suivante est
    /// *UI*, le clic sur le bouton n'atteint pas le jeu.
    bool barHovered = false;
    /// La taille demandée à la dernière image : celle du contenu de la fenêtre si elle est montrée,
    /// 0 × 0 sinon. C'est elle que la CI imprime (`editor.view`).
    app::SceneSize requested{};
    app::ScreenRect image{}; ///< Où la dernière image a mis la scène.
    bool barDrawn = false;   ///< La barre de mode en calque a été dessinée à la dernière image.
    ImVec2 barAt{};          ///< Son coin haut gauche.
    /// **Les rectangles que le texte d'ImGui recouvre dans l'image de la scène** : la barre de mode
    /// et la barre « Afficher », à l'écran, nuls tant qu'ils ne sont pas dessinés. Un test qui
    /// compare des pixels de la scène les laisse de côté : les glyphes de l'atlas d'ImGui ne sont
    /// pas les mêmes pixels d'une exécution à l'autre (build/GOTCHA.md).
    app::ScreenRect modeBarRect{};
    /// Le bouton de la barre de mode, là où ImGui l'a dessiné à la dernière image
    /// (`ModeBarOverlay::button`), nul tant qu'il ne l'est pas : un test le vise par ce rectangle,
    /// qui suit l'échelle de l'écran et la police.
    app::ScreenRect modeBarButton{};
    /// L'image de la scène est la fenêtre sous la souris à la dernière image, **au sens d'ImGui** :
    /// ni cachée par une autre fenêtre ou un menu sous la souris, ni sa barre de mode survolée (un
    /// popup ouvert ailleurs ne la bloque pas). Ce n'est pas un rectangle contre la souris, qui
    /// ignorerait ce qui est posé par-dessus. Là où un geste de caméra commence
    /// (`GestureContext::sceneHovered`, lu à l'image suivante, avant que `ui` ne le refasse), et
    /// l'une des deux conditions des touches de la Vue (`viewActiveIn`).
    bool hovered = false;
    /// **Le menu du clic droit** (ADR-0036, décision 8) : ImGui voit la fin d'un vol comme un clic
    /// (il reçoit le relâchement sans les mouvements), le menu ne s'ouvre donc que si le clic droit
    /// n'a pas volé (`GestureState::flew`). Posé par l'éditeur avant de dessiner.
    bool menuAllowed = true;
    bool canFrame = false;     ///< Il y a une sélection à cadrer : l'entrée du menu est active.
    std::string frameShortcut; ///< L'accord affiché dans l'entrée (« F »), selon le clavier.
    std::string modeHint;  ///< Le rappel de la touche du mode (`modeHintOf`), posé par l'éditeur.
    bool menuOpen = false; ///< Le menu est ouvert à la dernière image.
    bool frameRequested = false; ///< L'entrée « Cadrer la sélection » du menu a été choisie.
    /// Le menu « Afficher » (`show.hpp`) : ce que la Vue montre, où il a été dessiné, et « Vue de
    /// jeu » choisie à cette image dans le menu (l'éditeur atteint alors l'action, comme la
    /// touche).
    ShowState show;
    ShowMenuLayout showMenu;
    std::string gameViewShortcut; ///< L'accord affiché dans l'entrée (« G »), selon le clavier.
    bool gameViewRequested = false;
};

/// La taille en pixels que la Vue demande pour un contenu de `width` × `height` : l'entier en
/// dessous (une texture n'a pas de demi-pixel), vide dès qu'un côté fait moins d'un pixel. Nommé :
/// un contenu de 0,6 pixel ne doit pas demander une texture de 0 × 1.
[[nodiscard]] inline app::SceneSize sceneSizeOfContent(float width, float height)
{
    const auto whole = [](float side)
    { return side >= 1.0f ? static_cast<std::uint32_t>(std::floor(side)) : 0u; };
    const app::SceneSize size{whole(width), whole(height)};
    return size.empty() ? app::SceneSize{} : size;
}

/// Qui a le focus pour la route de l'input (ADR-0036, décision 4) : panneaux ouverts, la Vue
/// seule, et pas quand la souris est sur sa barre de mode (`barHovered` : le clic va au bouton,
/// non au jeu) ; panneaux fermés, la scène remplit la fenêtre et l'a tant qu'aucune fenêtre ne
/// l'a.
[[nodiscard]] constexpr bool sceneFocusedIn(bool panelsOpen, bool viewFocused,
                                            bool anyWindowFocused, bool barHovered)
{
    return panelsOpen ? viewFocused && !barHovered : !anyWindowFocused;
}

/// La Vue a le focus, ou l'aura à sa prochaine image : Play le lui a demandé (`focusRequested`)
/// et elle est ouverte. Ainsi le clic qui suit Alt+P va au jeu sans image perdue à attendre
/// que le focus soit pris.
[[nodiscard]] constexpr bool viewHasFocus(const View& view)
{
    return view.focused || (view.open && view.focusRequested);
}

/// Dessine la Vue, ancrée au premier affichage au nœud central de la disposition d'`app`. À appeler
/// une fois par image, panneaux ouverts : elle appelle `requestScene` (une fois), montre la
/// texture sur tout son contenu, pose `App::sceneRect` et dessine la barre de mode en calque.
/// Rend le mode que le bouton de la barre a demandé.
[[nodiscard]] std::optional<Mode> drawView(View& view, app::App& app, Mode mode);

} // namespace levain::editor
