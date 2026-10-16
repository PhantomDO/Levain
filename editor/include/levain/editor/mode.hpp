#pragma once

// Les modes de l'éditeur (ADR-0036, décision 3, ADR-0037) : **Édition**, où rien ne tourne et où le
// jeu ne reçoit rien, et **Jouer**, où le jeu tourne dans la Vue et où Stop rend la scène telle
// qu'elle était (`play.hpp`). Simuler et la pause (morceaux 5 et 6) viendront s'y ajouter : un
// mode de plus, et un drapeau, sans toucher aux fonctions ci-dessous. La logique est en fonctions
// libres dont les dépendances sont des paramètres (ADR-0011) ; `withEditor` n'en a que la glu.
// Équivalents : le mode Édition d'Unity (hors Play) et d'Unreal (hors PIE) ; Godot, lui, ne joue
// que dans une fenêtre à part.

#include <cstdint>
#include <optional>
#include <string>

#include <imgui.h>

#include "levain/app/app.hpp"
#include "levain/editor/shortcuts.hpp"
#include "levain/input/state.hpp"

namespace levain::editor
{

enum class Mode : std::uint8_t
{
    Edit, ///< « Édition » : aucun pas, `frame` du programme jamais appelé, route *éditeur*.
    Play, ///< « Jouer » : le jeu tourne ; Stop rend la scène (`stopPlay`).
};

/// Où en sont les modes : celui d'aujourd'hui, et la touche d'arrêt vue à l'image d'avant (pour
/// n'en lire que le front).
struct ModeState
{
    Mode current = Mode::Edit;
    bool stopHeld = false;
};

/// La simulation est arrêtée en Édition (`App::simulationPaused`).
[[nodiscard]] constexpr bool simulationPausedIn(Mode mode)
{
    switch (mode)
    {
    case Mode::Edit:
        return true;
    case Mode::Play:
        return false;
    }
    return true; // inatteignable : un nouveau mode doit être traité ci-dessus (pas de `default`)
}

/// Le `frame` du programme (ce que le joueur demande : `steerDemo` pour le sandbox) ne tourne qu'en
/// jeu. En Édition il ne tourne **jamais**, sans quoi ZQSD déplacerait la caméra pendant qu'on
/// édite.
[[nodiscard]] constexpr bool programFrameRunsIn(Mode mode)
{
    switch (mode)
    {
    case Mode::Edit:
        return false;
    case Mode::Play:
        return true;
    }
    return false;
}

/// La route de l'input pour l'image suivante (`App::inputRoute`). Édition : *éditeur*. Jeu : *jeu*
/// quand la scène a le focus, sinon *UI* (une fenêtre d'ImGui le garde : un champ de texte actif
/// ne doit pas faire marcher le renard).
[[nodiscard]] constexpr app::InputRoute inputRouteFor(Mode mode, bool sceneFocused)
{
    switch (mode)
    {
    case Mode::Edit:
        return app::InputRoute::Editor;
    case Mode::Play:
        return sceneFocused ? app::InputRoute::Game : app::InputRoute::Ui;
    }
    return app::InputRoute::Editor;
}

[[nodiscard]] constexpr Mode otherMode(Mode mode)
{
    switch (mode)
    {
    case Mode::Edit:
        return Mode::Play;
    case Mode::Play:
        return Mode::Edit;
    }
    return Mode::Edit;
}

/// Le libellé du bouton qui mène au mode `target` : une icône (lecture, ou arrêt pour revenir à
/// l'édition) et le texte traduit, avec l'identifiant de `labelOf`. La barre d'outils et la barre
/// de la Vue le partagent.
[[nodiscard]] std::string modeButtonLabel(Mode target);

/// Ce qui demande un changement de mode à cette image : le bouton de la barre (le mode qu'il
/// propose), Alt+P, le front d'Échap.
struct ModeRequests
{
    std::optional<Mode> toolbar = std::nullopt;
    bool playShortcut = false;
    bool stop = false;
};

/// Le mode où passer, s'il y en a un. **Alt+P ne fait que jouer** (« Jouer » dans la table des
/// raccourcis, ADR-0036, décision 13) et **Échap ne fait qu'arrêter** : sous la route *jeu*, ImGui
/// n'a pas Alt+P, donc une Alt+P qui basculerait dans les deux sens ne reviendrait qu'à certaines
/// images, selon le focus. Seul le bouton va dans les deux sens, par le mode qu'il propose. Échap
/// passe avant le reste : sortir du jeu ne se dispute pas.
[[nodiscard]] constexpr std::optional<Mode> modeRequested(Mode current,
                                                          const ModeRequests& requests)
{
    // Arrêter vaut de tout mode qui n'est pas l'Édition, jouer de l'Édition seule : un mode de plus
    // (Simuler) s'arrête par Échap sans toucher à cette fonction.
    if (requests.stop && current != Mode::Edit)
    {
        return Mode::Edit;
    }
    if (requests.playShortcut && current == Mode::Edit)
    {
        return Mode::Play;
    }
    if (requests.toolbar && *requests.toolbar != current)
    {
        return requests.toolbar;
    }
    return std::nullopt;
}

/// Le nom du mode, clé du catalogue (`ui::tr`).
[[nodiscard]] constexpr const char* modeNameOf(Mode mode)
{
    switch (mode)
    {
    case Mode::Edit:
        return "Édition";
    case Mode::Play:
        return "Jouer";
    }
    return "";
}

/// Le rappel de la touche du mode, traduit : celle qui sort du mode où l'on est, **telle que la
/// table l'a maintenant** (un accord changé par la fenêtre « Raccourcis » change le rappel :
/// `Alt+P : jouer`, `Échap : arrêter` par défaut). Alt+P ne ramène pas de Jouer
/// (`modeRequested`) : la dire ici serait fausse sous la route *jeu*. Vide si l'action n'a plus
/// d'accord, on ne promet pas une touche qui n'existe pas.
[[nodiscard]] std::string modeHintOf(Mode mode, const ShortcutTable& table,
                                     const platform::KeyAtPosition& keyAtPosition);

/// Le mot du mode dans la ligne que lit la CI (`editor.mode`) : le même dans toutes les langues.
[[nodiscard]] constexpr const char* modeTokenOf(Mode mode)
{
    switch (mode)
    {
    case Mode::Edit:
        return "edit";
    case Mode::Play:
        return "play";
    }
    return "";
}

/// Échap vient d'être appuyée : le front, lu sur l'input brut du jeu, à la position que la table
/// donne à « Arrêter » (`table`, ADR-0036, décision 13) : un accord changé par l'utilisateur
/// (morceau 16) arrête aussi. Une touche tenue ne le redonne pas. **Elle vaut partout où le jeu
/// reçoit le clavier** : sous la route *jeu*, mais aussi sous la route *UI* quand une fenêtre a le
/// focus sans qu'un champ soit actif ; la décision 13 la réserve au contexte *jeu* (la route *jeu*
/// seule sait l'être : M7.5 la lui rendra avec « Arrêter »). Sans accord de position pour l'action,
/// jamais.
[[nodiscard]] bool stopPressed(ModeState& state, const input::RawInput& raw,
                               const ShortcutTable& table);

/// Passe dans ce mode. **Remet `mouseCaptureWanted` à faux** à chaque changement : en Édition le
/// `frame` du programme, qui le repose, ne tourne plus, et la souris qu'un clic droit tenu avait
/// capturée resterait prise. Rend aussi le jeu à l'immobile : ce qu'il tenait est relâché et les
/// appuis qu'aucun pas n'a vus sont oubliés, sans quoi ils partiraient tous au premier pas de Play.
void enterMode(app::App& app, ModeState& state, Mode mode);

/// La barre de mode, accrochée au coin haut gauche de la scène (`scene`, `App::sceneRect`) : dans
/// le trou du centre, jamais sur les onglets des colonnes. **Dessinée toujours** : le bandeau de
/// le bandeau de « Jouer » et le rappel de la touche valent aussi panneaux fermés, où le bouton
/// manque : la fenêtre est un bandeau que rien ne vise ni ne focalise, et la route *jeu* reste.
/// Panneaux ouverts, la barre est celle de la Vue (`drawModeBarOverlay`). `hint` : le rappel de
/// la touche (`modeHintOf`).
void drawModeBar(Mode current, const app::ScreenRect& scene, const std::string& hint);

/// Ce que la barre de mode en calque a fait à cette image (`drawModeBarOverlay`), et où ImGui l'a
/// dessinée : des rectangles à l'écran, nuls tant que rien n'est dessiné.
struct ModeBarOverlay
{
    std::optional<Mode> requested; ///< Le mode que son bouton demande, s'il a été cliqué.
    /// La souris est sur la barre : la Vue en tire la route *UI* de l'image suivante, sans quoi un
    /// clic sur le bouton atteindrait aussi le jeu (la barre est dans le rectangle de la scène).
    bool hovered = false;
    /// La barre entière, lue sur son enfant à la sortie (le dernier item dessiné n'est plus la
    /// barre dès qu'on y ajoute) : la Vue place le menu « Afficher » à sa droite, et un test de
    /// pixels de la scène la laisse de côté (`View::modeBarRect`).
    app::ScreenRect bar{};
    /// **Son bouton, là où ImGui l'a dessiné** : sa place et sa taille suivent l'échelle de l'écran
    /// (`ScaleAllSizes` : une marge de 10 px à 125 %, pas 8) et la police. Un test le vise par ce
    /// rectangle (`View::modeBarButton`), jamais par des décalages écrits à la main.
    app::ScreenRect button{};
};

/// La même barre en calque de la fenêtre courante (la Vue), dans son coin haut gauche (`corner`) :
/// un enfant de cette fenêtre, que le focus de la Vue ne passe jamais derrière. Toujours avec son
/// bouton : la Vue n'existe que panneaux ouverts. Fond semi-opaque, pour que le rappel de la touche
/// se lise sur un ciel clair. `hint` : le rappel de la touche (`modeHintOf`).
[[nodiscard]] ModeBarOverlay drawModeBarOverlay(Mode current, ImVec2 corner,
                                                const std::string& hint);

/// Alt+P est pressée, par la lettre que la table donne à « Jouer » (`imguiChordOf`, ADR-0036,
/// décision 13) : le raccourci d'Unreal pour jouer. ImGui doit recevoir le clavier : sous la route
/// *jeu*, il n'en a que les relâchements. Sans accord de lettre pour l'action, jamais.
[[nodiscard]] bool playShortcutPressed(const ShortcutTable& table);

/// Aucune fenêtre d'ImGui n'a le focus : la scène l'a, la fenêtre entière, panneaux fermés.
/// Panneaux ouverts, c'est la Vue qui a le focus ou non (`View::focused`, `sceneFocusedIn`).
[[nodiscard]] bool sceneHasFocus();

} // namespace levain::editor
