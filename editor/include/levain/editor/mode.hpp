#pragma once

// Les modes de l'éditeur (ADR-0036, décision 3) : **Édition**, où rien ne tourne et où le jeu ne
// reçoit rien, et « Jouer (sans retour) », le jeu d'aujourd'hui sous un bandeau, jusqu'à Play et
// Stop (M7.5). La logique est en fonctions libres dont les dépendances sont des paramètres
// (ADR-0011) ; `withEditor` n'en a que la glu. Équivalents : le mode Édition d'Unity (hors Play) et
// d'Unreal (hors PIE) ; Godot, lui, ne joue que dans une fenêtre à part.

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
    PlayWithoutReturn, ///< « Jouer (sans retour) » : le jeu tourne, rien n'est restauré à l'arrêt.
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
    return mode == Mode::Edit;
}

/// Le `frame` du programme (ce que le joueur demande : `steerDemo` pour le sandbox) ne tourne qu'en
/// jeu. En Édition il ne tourne **jamais**, sans quoi ZQSD déplacerait la caméra pendant qu'on
/// édite.
[[nodiscard]] constexpr bool programFrameRunsIn(Mode mode)
{
    return mode != Mode::Edit;
}

/// La route de l'input pour l'image suivante (`App::inputRoute`). Édition : *éditeur*. Jeu : *jeu*
/// quand la scène a le focus, sinon *UI* (une fenêtre d'ImGui le garde : un champ de texte actif
/// ne doit pas faire marcher le renard).
[[nodiscard]] constexpr app::InputRoute inputRouteFor(Mode mode, bool sceneFocused)
{
    if (mode == Mode::Edit)
    {
        return app::InputRoute::Editor;
    }
    return sceneFocused ? app::InputRoute::Game : app::InputRoute::Ui;
}

[[nodiscard]] constexpr Mode otherMode(Mode mode)
{
    return mode == Mode::Edit ? Mode::PlayWithoutReturn : Mode::Edit;
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
    if (requests.stop && current == Mode::PlayWithoutReturn)
    {
        return Mode::Edit;
    }
    if (requests.playShortcut && current == Mode::Edit)
    {
        return Mode::PlayWithoutReturn;
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
    return mode == Mode::Edit ? "Édition" : "Jouer (sans retour)";
}

/// Le rappel de la touche du mode, clé du catalogue : celle qui sort du mode où l'on est. Alt+P ne
/// ramène pas de Jouer (`modeRequested`) : la dire ici serait fausse sous la route *jeu*.
[[nodiscard]] constexpr const char* modeHintOf(Mode mode)
{
    return mode == Mode::Edit ? "Alt+P : jouer" : "Échap : revenir à l'édition";
}

/// Le mot du mode dans la ligne que lit la CI (`editor.mode`) : le même dans toutes les langues.
[[nodiscard]] constexpr const char* modeTokenOf(Mode mode)
{
    return mode == Mode::Edit ? "edit" : "play";
}

/// Échap, par sa position (le scancode 41 d'USB, que `platform` vérifie contre SDL) : le défaut de
/// l'action « Arrêter » de la table (`shortcuts.hpp`). En jeu, elle ramène à l'Édition, comme
/// l'arrêt d'Unreal (ADR-0036, décision 13) ; M7.5 en fera « Arrêter ».
inline constexpr std::uint16_t StopScancode = 41;

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
/// « Jouer (sans retour) » et le rappel de la touche valent aussi panneaux fermés, où le bouton
/// manque : la fenêtre est un bandeau que rien ne vise ni ne focalise, et la route *jeu* reste.
/// Panneaux ouverts, la barre est celle de la Vue (`drawModeBarOverlay`).
void drawModeBar(Mode current, const app::ScreenRect& scene);

/// La même barre en calque de la fenêtre courante (la Vue), dans son coin haut gauche (`corner`) :
/// un enfant de cette fenêtre, que le focus de la Vue ne passe jamais derrière. Toujours avec son
/// bouton : la Vue n'existe que panneaux ouverts. Fond semi-opaque, pour que le rappel de la touche
/// se lise sur un ciel clair. `hovered` dit si la souris est sur la barre : la Vue en tire la
/// route *UI* de l'image suivante, sans quoi un clic sur le bouton atteindrait aussi le jeu (la
/// barre est dans le rectangle de la scène).
[[nodiscard]] std::optional<Mode> drawModeBarOverlay(Mode current, ImVec2 corner, bool& hovered);

/// Alt+P est pressée, par la lettre que la table donne à « Jouer » (`imguiChordOf`, ADR-0036,
/// décision 13) : le raccourci d'Unreal pour jouer. ImGui doit recevoir le clavier : sous la route
/// *jeu*, il n'en a que les relâchements. Sans accord de lettre pour l'action, jamais.
[[nodiscard]] bool playShortcutPressed(const ShortcutTable& table);

/// Aucune fenêtre d'ImGui n'a le focus : la scène l'a, la fenêtre entière, panneaux fermés.
/// Panneaux ouverts, c'est la Vue qui a le focus ou non (`View::focused`, `sceneFocusedIn`).
[[nodiscard]] bool sceneHasFocus();

} // namespace levain::editor
