#pragma once

// Les modes de l'éditeur (ADR-0036, décision 3) : **Édition**, où rien ne tourne et où le jeu ne
// reçoit rien, et « Jouer (sans retour) », le jeu d'aujourd'hui, jusqu'à Play et
// Stop (M7.5). La logique est en fonctions libres dont les dépendances sont des paramètres
// (ADR-0011) ; `withEditor` n'en a que la glu. Équivalents : le mode Édition d'Unity (hors Play) et
// d'Unreal (hors PIE) ; Godot, lui, ne joue que dans une fenêtre à part.

#include <cstdint>
#include <optional>

#include "levain/app/app.hpp"
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

/// Ce qui demande un changement de mode à cette image : Alt+P, le front d'Échap.
struct ModeRequests
{
    bool playShortcut = false;
    bool stop = false;
};

/// Le mode où passer, s'il y en a un. **Alt+P ne fait que jouer** (« Jouer » dans la table des
/// raccourcis, ADR-0036, décision 13) et **Échap ne fait qu'arrêter** : sous la route *jeu*, ImGui
/// n'a pas Alt+P, donc une Alt+P qui basculerait dans les deux sens ne reviendrait qu'à certaines
/// images, selon le focus. Échap passe avant le reste : sortir du jeu ne se dispute pas.
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
    return std::nullopt;
}

/// Le nom du mode, clé du catalogue (`ui::tr`).
[[nodiscard]] constexpr const char* modeNameOf(Mode mode)
{
    return mode == Mode::Edit ? "Édition" : "Jouer (sans retour)";
}

/// Le mot du mode dans la ligne que lit la CI (`editor.mode`) : le même dans toutes les langues.
[[nodiscard]] constexpr const char* modeTokenOf(Mode mode)
{
    return mode == Mode::Edit ? "edit" : "play";
}

/// Échap, par sa position (le scancode 41 d'USB, que `platform` vérifie contre SDL) : en jeu, elle
/// ramène à l'Édition, comme l'arrêt d'Unreal (ADR-0036, décision 13) ; M7.5 en fera « Arrêter ».
inline constexpr std::uint16_t StopScancode = 41;

/// Échap vient d'être appuyée : le front, lu sur l'input brut du jeu. Une touche tenue ne le
/// redonne pas. **Elle vaut partout où le jeu reçoit le clavier** : sous la route *jeu*, mais aussi
/// sous la route *UI* quand une fenêtre a le focus sans qu'un champ soit actif ; la décision 13
/// la réserve au contexte *jeu*, ce que la table des raccourcis (morceau 10) fera.
[[nodiscard]] bool stopPressed(ModeState& state, const input::RawInput& raw);

/// Passe dans ce mode. **Remet `mouseCaptureWanted` à faux** à chaque changement : en Édition le
/// `frame` du programme, qui le repose, ne tourne plus, et la souris qu'un clic droit tenu avait
/// capturée resterait prise. Rend aussi le jeu à l'immobile : ce qu'il tenait est relâché et les
/// appuis qu'aucun pas n'a vus sont oubliés, sans quoi ils partiraient tous au premier pas de Play.
void enterMode(app::App& app, ModeState& state, Mode mode);

/// Alt+P est pressée, par la lettre (ADR-0036, décision 13) : le raccourci d'Unreal pour jouer.
/// ImGui doit recevoir le clavier : sous la route *jeu*, il n'en a que les relâchements.
[[nodiscard]] bool playShortcutPressed();

/// Aucune fenêtre d'ImGui n'a le focus : la scène l'a (le trou du docking, ou la fenêtre entière
/// panneaux fermés). **Tant que la Vue n'existe pas** (morceaux 7 et 8) ; elle prendra le focus à
/// sa place.
[[nodiscard]] bool sceneHasFocus();

} // namespace levain::editor
