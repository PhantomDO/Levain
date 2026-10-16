#pragma once

// Les modes de l'éditeur (ADR-0036, décision 3, ADR-0037) : **Édition**, où rien ne tourne et où le
// jeu ne reçoit rien, et **Jouer**, où le jeu tourne dans la Vue et où Stop rend la scène telle
// qu'elle était (`play.hpp`). **La pause** (ADR-0037, décision 6) n'est pas un mode mais un drapeau
// de `ModeState`, qui vaut en Jouer et vaudra en Simuler (morceau 6) : un mode de plus s'ajoute aux
// `switch` ci-dessous, sans toucher à la pause. La logique est en fonctions libres dont les
// dépendances sont des paramètres (ADR-0011) ; `withEditor` n'en a que la glu. Équivalents : le
// mode Édition d'Unity (hors Play) et d'Unreal (hors PIE), Pause et Maj+F1 d'Unreal, le bouton
// Pause d'Unity ; Godot, lui, ne joue que dans une fenêtre à part.

#include <bitset>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include "levain/app/app.hpp"
#include "levain/editor/shortcuts.hpp"
#include "levain/platform/input.hpp"
#include "levain/platform/window.hpp"

namespace levain::editor
{

enum class Mode : std::uint8_t
{
    Edit, ///< « Édition » : aucun pas, `frame` du programme jamais appelé, route *éditeur*.
    Play, ///< « Jouer » : le jeu tourne ; Stop rend la scène (`stopPlay`).
};

/// Où en sont les modes : celui d'aujourd'hui, la pause et la souris rendue (qui n'ont de sens
/// qu'en jeu, `enterMode` les remet à faux), et les touches tenues telles que l'éditeur les voit.
struct ModeState
{
    Mode current = Mode::Edit;
    /// **La pause** : la simulation arrêtée, le `frame` du programme muet, rien au jeu. L'éditeur
    /// regarde le monde tel qu'il est, caméra du jeu comprise.
    bool paused = false;
    /// **Maj+F1** : la souris est rendue à l'éditeur, le jeu ne reçoit plus rien jusqu'au prochain
    /// clic dans la scène (`keepMouseFromGame`).
    bool mouseFreed = false;
    /// Les touches tenues, lues dans les événements bruts de l'image (`App::rawFrameEvents`) :
    /// **quelle que soit la route**, car le jeu en pause ou la souris rendue ne reçoivent rien, et
    /// Échap, Pause et Maj+F1 doivent encore se lire (`gameKeysPressed`).
    std::bitset<platform::KeyCodeCount> keys;
};

/// La simulation tourne : jamais en Édition, en jeu tant qu'elle n'est pas en pause
/// (`App::simulationPaused` est l'inverse).
[[nodiscard]] constexpr bool simulationRunsIn(Mode mode, bool paused)
{
    switch (mode)
    {
    case Mode::Edit:
        return false;
    case Mode::Play:
        return !paused;
    }
    return false; // inatteignable : un nouveau mode doit être traité ci-dessus (pas de `default`)
}

/// Le `frame` du programme (ce que le joueur demande : `steerDemo` pour le sandbox) ne tourne
/// qu'en jeu, et pas en pause. En Édition il ne tourne **jamais**, sans quoi ZQSD déplacerait la
/// caméra pendant qu'on édite. **Sauf pour un pas en pause** (`stepping` : `App::singleStep` est
/// posé) : les systèmes du pas lisent ce que `frame` a posé (`FpsInput`, `WalkInput` du sandbox),
/// et sans lui ils rejouent l'input d'avant la pause (W tenue, Pause, W relâchée : deux pas
/// avancent encore la caméra). Il tourne alors sous la route de la pause, celle de l'Édition : il y
/// lit un input vide, et le pas joue un jeu qui ne reçoit rien. Unity (Step) et Unreal (avance
/// d'une image) font tourner le jeu d'une image.
[[nodiscard]] constexpr bool programFrameRunsIn(Mode mode, bool paused, bool stepping)
{
    switch (mode)
    {
    case Mode::Edit:
        return false;
    case Mode::Play:
        return !paused || stepping;
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

/// La route, pause et souris rendue comprises : **dans les deux cas le jeu ne reçoit rien**, la
/// route est celle de l'Édition (ce qu'il tenait est relâché, ImGui a la souris et le clavier). Le
/// piège nommé : sans cela, la pause ne ferait que se taire, et une touche tenue pendant elle
/// partirait au premier pas de la reprise.
[[nodiscard]] constexpr app::InputRoute inputRouteOf(const ModeState& state, bool sceneFocused)
{
    return state.paused || state.mouseFreed ? app::InputRoute::Editor
                                            : inputRouteFor(state.current, sceneFocused);
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

/// Ce que les touches du contexte *jeu* ont demandé à cette image.
struct GameKeys
{
    bool stop = false;           ///< « Arrêter » (Échap) vient d'être appuyée.
    std::vector<Action> actions; ///< Pause, Image suivante, Rendre la souris : dans l'ordre.
};

/// Les touches du contexte *jeu* qui viennent d'être appuyées (la décision 13 : Échap, la touche
/// Pause, Maj+F1, et l'accord qu'on aurait donné à « Image suivante »), **lues dans les
/// événements bruts de l'image** et à la place que la table leur donne : un accord changé dans la
/// fenêtre « Raccourcis » vaut aussi. **Pas sur l'input du jeu** : en pause, ou la souris rendue,
/// le jeu ne reçoit rien (`inputRouteOf`), et Échap doit pourtant arrêter, Pause reprendre. Un
/// appui est le front (`keyPressesOf` : une répétition n'en est pas un) ; Échap ignore les
/// modificateurs (`isHeldAction`), les autres les veulent exacts. Rien en Édition, ni quand
/// `blocked` : un champ de texte actif (Échap y annule la frappe) ou une modale ouverte (Échap la
/// ferme, et ne doit pas arrêter le jeu dessous) ; `state.keys` se tient à jour dans tous les cas,
/// pour que le front soit juste à la première image où les touches valent.
[[nodiscard]] GameKeys gameKeysPressed(ModeState& state, const platform::Events& events,
                                       const ShortcutTable& table, bool blocked);

/// Un bouton de la souris vient d'être enfoncé, d'après les événements bruts de l'image : ce qui
/// rend la souris au jeu après Maj+F1 (s'il tombe sur la scène). Un relâchement, un mouvement ou la
/// molette n'en sont pas.
[[nodiscard]] bool mousePressedIn(const platform::Events& events);

/// Ce que Pause, Image suivante et Rendre la souris font au jeu.
enum class PlayControl : std::uint8_t
{
    TogglePause, ///< Pause quand le jeu tourne, Reprendre quand il est en pause.
    StepFrame,   ///< Un pas, puis la pause reprend (`App::singleStep`).
    FreeMouse,   ///< Maj+F1 : la souris à l'éditeur jusqu'au prochain clic dans la scène.
};

/// Le contrôle de jeu qu'une action est, ou rien (Jouer, Arrêter et le reste suivent d'autres
/// chemins).
[[nodiscard]] constexpr std::optional<PlayControl> playControlOf(Action action)
{
    switch (action)
    {
    case Action::Pause:
        return PlayControl::TogglePause;
    case Action::NextFrame:
        return PlayControl::StepFrame;
    case Action::GiveMouseBack:
        return PlayControl::FreeMouse;
    case Action::MoveForward:
    case Action::MoveBackward:
    case Action::MoveLeft:
    case Action::MoveRight:
    case Action::MoveUp:
    case Action::MoveDown:
    case Action::FrameSelection:
    case Action::GameView:
    case Action::RealTime:
    case Action::ToolMove:
    case Action::ToolRotate:
    case Action::ToolScale:
    case Action::NextTool:
    case Action::Save:
    case Action::Undo:
    case Action::Redo:
    case Action::Play:
    case Action::Simulate:
    case Action::Stop:
    case Action::Count:
        return std::nullopt; // pas de `default` : une action de plus doit être classée ici
    }
    return std::nullopt;
}

/// Pourquoi un contrôle ne peut pas partir maintenant.
enum class PlayRefusal : std::uint8_t
{
    None,       ///< Il peut partir.
    NotPlaying, ///< « seulement en jeu » : aucun contrôle n'a de sens en Édition.
    NotPaused,  ///< « seulement en pause » : Image suivante.
};

/// Ce qui empêche le contrôle. **« Image suivante » veut la pause** : en marche elle serait un pas
/// de plus que le temps ne mérite, et `planFrame` l'ignorerait de toute façon.
[[nodiscard]] constexpr PlayRefusal playControlRefusal(PlayControl control, Mode mode, bool paused)
{
    switch (mode)
    {
    case Mode::Edit:
        return PlayRefusal::NotPlaying;
    case Mode::Play:
        return control == PlayControl::StepFrame && !paused ? PlayRefusal::NotPaused
                                                            : PlayRefusal::None;
    }
    return PlayRefusal::NotPlaying;
}

/// Fait ce que le contrôle dit, supposé permis (`playControlRefusal`). Pause et Reprendre rendent
/// le jeu à l'immobile comme un changement de mode (`enterMode`) : ce qu'il tenait est relâché, les
/// appuis qu'aucun pas n'a vus sont oubliés. Une ligne de glu dans `withEditor`.
void applyPlayControl(PlayControl control, app::App& app, ModeState& state);

/// **La souris reste à l'éditeur quand le jeu ne reçoit rien** (Maj+F1, ou la pause) : le jeu
/// repose `mouseCaptureWanted` à chaque image dans son `frame`, qui tourne encore avec la souris
/// rendue, et pour un pas en pause (`programFrameRunsIn`), et il capturerait la souris de nouveau.
/// À appeler juste après lui.
void keepMouseFromGame(app::App& app, const ModeState& state);

/// Passe dans ce mode. **Remet à faux la pause, la souris rendue et `mouseCaptureWanted`** à chaque
/// changement : une session de jeu ne garde rien de la précédente, et en Édition le `frame` du
/// programme, qui repose la capture, ne tourne plus : la souris qu'un clic droit tenu avait
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
