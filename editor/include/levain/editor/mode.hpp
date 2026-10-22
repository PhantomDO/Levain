#pragma once

// Les modes de l'éditeur (ADR-0036, décision 3, ADR-0037) : **Édition**, où rien ne tourne et où le
// jeu ne reçoit rien, **Jouer**, où le jeu tourne dans la Vue et où Stop rend la scène telle
// qu'elle était (`play.hpp`), et **Simuler**, où la physique et le gameplay tournent sous la caméra
// et les gestes de l'éditeur, sans input du jeu, et où Stop rend la scène de même. **La pause**
// (ADR-0037, décision 6) n'est pas un mode mais un drapeau de `ModeState`, qui vaut en Jouer et en
// Simuler : un mode de plus s'ajoute aux `switch` ci-dessous, sans toucher à la pause. La logique
// est en fonctions libres dont les dépendances sont des paramètres (ADR-0011) ; `withEditor` n'en a
// que la glu. Équivalents : le mode Édition d'Unity (hors Play) et d'Unreal (hors PIE), Pause,
// Maj+F1 et Simulate (Alt+S) d'Unreal, le bouton Pause d'Unity (qui n'a pas de Simulate) ; Godot,
// lui, ne joue que dans une fenêtre à part.

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
#include "levain/ui/style.hpp"

namespace levain::editor
{

enum class Mode : std::uint8_t
{
    Edit, ///< « Édition » : aucun pas, `frame` du programme jamais appelé, route *éditeur*.
    Play, ///< « Jouer » : le jeu tourne ; Stop rend la scène (`stopPlay`).
    /// « Simuler » (Alt+S) : la simulation tourne sous la caméra et les gestes de l'éditeur, le jeu
    /// ne reçoit rien ; Stop rend la scène comme après Jouer (`stopPlay`).
    Simulate,
};

/// Où en sont les modes : celui d'aujourd'hui, la pause et la souris rendue (qui n'ont de sens
/// qu'en jeu, `enterMode` les remet à faux ; la souris rendue seulement en Jouer, où le jeu en a
/// une), et les touches tenues telles que l'éditeur les voit.
struct ModeState
{
    Mode current = Mode::Edit;
    /// **La pause** : la simulation arrêtée, le `frame` du programme muet (sauf pour un pas,
    /// `programFrameRunsIn`), rien au jeu. L'éditeur regarde le monde tel qu'il est, caméra du jeu
    /// comprise.
    bool paused = false;
    /// **Maj+F1** : la souris est rendue à l'éditeur, le jeu ne reçoit plus rien jusqu'au prochain
    /// clic dans la scène (`keepMouseFromGame`).
    bool mouseFreed = false;
    /// Les touches tenues, lues dans les événements bruts de l'image (`App::rawFrameEvents`) :
    /// **quelle que soit la route**, car le jeu en pause ou la souris rendue ne reçoivent rien, et
    /// Échap, Pause et Maj+F1 doivent encore se lire (`gameKeysPressed`).
    std::bitset<platform::KeyCodeCount> keys;
};

/// La simulation tourne : jamais en Édition, en jeu (Jouer ou Simuler) tant qu'elle n'est pas en
/// pause (`App::simulationPaused` est l'inverse).
[[nodiscard]] constexpr bool simulationRunsIn(Mode mode, bool paused)
{
    switch (mode)
    {
    case Mode::Edit:
        return false;
    case Mode::Play:
    case Mode::Simulate:
        return !paused;
    }
    return false; // inatteignable : un nouveau mode doit être traité ci-dessus (pas de `default`)
}

/// Le `frame` du programme (ce que le joueur demande : `steerDemo` pour le sandbox) tourne en jeu,
/// Jouer ou Simuler, et pas en pause. En Édition il ne tourne **jamais**, sans quoi ZQSD
/// déplacerait la caméra pendant qu'on édite. **Sauf pour un pas en pause** (`stepping` :
/// `App::singleStep` est posé) : les systèmes du pas lisent ce que `frame` a posé (`FpsInput`,
/// `WalkInput` du sandbox), et sans lui ils rejouent l'input d'avant la pause (W tenue, Pause, W
/// relâchée : deux pas avancent encore la caméra). Il tourne alors sous la route de la pause, celle
/// de l'Édition : il y lit un input vide, et le pas joue un jeu qui ne reçoit rien. Unity (Step) et
/// Unreal (avance d'une image) font tourner le jeu d'une image.
/// **En Simuler il tourne à chaque image, toujours sous l'input vide** (`inputRouteFor`), pour la
/// même raison : W tenue en Jouer, Échap, puis Alt+S, et la caméra du jeu avancerait pour de bon
/// (`FpsInput`, un singleton du monde que Stop ne rend pas, garde la dernière image de Jouer, où
/// `frame` s'est tu en Édition ; `WalkInput`, lui, est un composant du joueur, que Stop rend). Le
/// tableau de la décision 6 de l'ADR-0037 disait « jamais » : il est amendé.
[[nodiscard]] constexpr bool programFrameRunsIn(Mode mode, bool paused, bool stepping)
{
    switch (mode)
    {
    case Mode::Edit:
        return false;
    case Mode::Play:
    case Mode::Simulate:
        return !paused || stepping;
    }
    return false;
}

/// **La durée que le `frame` du programme voit** (`App::frameSeconds`). En marche, celle de
/// l'image, que le monde compte aussi. Sur un pas en pause (`stepping`), le monde avance d'un pas
/// fixe, non du temps mesuré de l'image en pause : un minuteur de jeu lu dans `frame` dériverait à
/// chaque pas (le piège nommé). Les gestes de l'éditeur, eux, gardent le temps mesuré : la caméra
/// vole en pause comme ailleurs.
[[nodiscard]] constexpr float programFrameSecondsOf(bool paused, bool stepping,
                                                    float measuredSeconds, float stepSeconds)
{
    return paused && stepping ? stepSeconds : measuredSeconds;
}

/// **La caméra et les gestes sont ceux de l'éditeur** : en Édition et en Simuler. Jouer rend la
/// caméra au jeu et la souris à son `frame`. Un seul nom pour cette moitié des modes, plutôt qu'un
/// `== Mode::Edit` écrit à chaque endroit où le vol, le cadrage, les aides ou les touches de la Vue
/// valent : Simuler en oublierait un.
[[nodiscard]] constexpr bool editorOwnsViewIn(Mode mode)
{
    switch (mode)
    {
    case Mode::Edit:
    case Mode::Simulate:
        return true;
    case Mode::Play:
        return false;
    }
    return false;
}

/// La route de l'input pour l'image suivante (`App::inputRoute`). Édition et Simuler : *éditeur*,
/// le jeu ne reçoit rien. Jouer : *jeu* quand la scène a le focus, sinon *UI* (une fenêtre d'ImGui
/// le garde : un champ de texte actif ne doit pas faire marcher le renard).
[[nodiscard]] constexpr app::InputRoute inputRouteFor(Mode mode, bool sceneFocused)
{
    switch (mode)
    {
    case Mode::Edit:
    case Mode::Simulate:
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

/// **La teinte du jeu** (ADR-0037, décision 6) : la couleur du bord de la Vue et de la barre
/// d'outils quand le jeu tourne, celle de la pause quand il est arrêté, rien en Édition. Unreal
/// teinte, Unity assombrit ce qui n'est pas la vue Game (documenté, ADR-0037 [2]). Les couleurs
/// sont celles du style (`ui/style.hpp`), jamais des littéraux ici. Une pause en Édition n'existe
/// pas : pas de teinte. **Simuler a la teinte de Jouer** : elle dit que le monde bouge et que Stop
/// le rend, vrai des deux ; une troisième couleur voudrait son propre contraste de 3 contre le
/// ciel, le sol et les panneaux (`tests/ui_style_test.cpp`), et le bandeau de la barre de mode dit
/// déjà lequel des deux on joue.
[[nodiscard]] constexpr std::optional<ImVec4> playTintOf(Mode mode, bool paused)
{
    switch (mode)
    {
    case Mode::Edit:
        return std::nullopt;
    case Mode::Play:
    case Mode::Simulate:
        return paused ? ui::PausedTint : ui::PlayTint;
    }
    return std::nullopt;
}

[[nodiscard]] constexpr Mode otherMode(Mode mode)
{
    switch (mode)
    {
    case Mode::Edit:
        return Mode::Play;
    case Mode::Play:
    case Mode::Simulate:
        return Mode::Edit;
    }
    return Mode::Edit;
}

/// Le libellé du bouton qui mène au mode `target` : une icône (lecture, ou arrêt pour revenir à
/// l'édition) et le texte traduit, avec l'identifiant de `labelOf`. La barre d'outils et la barre
/// de la Vue le partagent ; Simuler n'a pas d'icône, et son bouton n'est que dans la barre d'outils
/// (`otherMode` ne mène jamais à Simuler : on y entre par Alt+S ou ce bouton).
[[nodiscard]] std::string modeButtonLabel(Mode target);

/// Les libellés de la barre d'outils en jeu : « Pause » devient « Reprendre » quand le jeu est en
/// pause, et « Image suivante » a le sien. Chacun une icône et le texte traduit, avec
/// l'identifiant de `labelOf`.
[[nodiscard]] std::string pauseButtonLabel(bool paused);
[[nodiscard]] std::string stepButtonLabel();

/// Ce qui demande un changement de mode à cette image : le bouton de la barre (le mode qu'il
/// propose), Alt+P, Alt+S ou le bouton « Simuler » (l'action `Simulate`, que `runAction` rend), le
/// front d'Échap.
struct ModeRequests
{
    std::optional<Mode> toolbar = std::nullopt;
    bool playShortcut = false;
    bool simulate = false;
    bool stop = false;
};

/// Le mode où passer, s'il y en a un. **Alt+P ne fait que jouer** (« Jouer » dans la table des
/// raccourcis, ADR-0036, décision 13), **Alt+S que simuler** et **Échap qu'arrêter** : sous la
/// route *jeu*, ImGui n'a pas Alt+P, donc une Alt+P qui basculerait dans les deux sens ne
/// reviendrait qu'à certaines images, selon le focus. Seul le bouton va dans les deux sens, par le
/// mode qu'il propose. Échap passe avant le reste : sortir du jeu ne se dispute pas.
[[nodiscard]] constexpr std::optional<Mode> modeRequested(Mode current,
                                                          const ModeRequests& requests)
{
    // Arrêter vaut de tout mode qui n'est pas l'Édition, jouer et simuler de l'Édition seule : de
    // Jouer on ne passe pas à Simuler (il faut arrêter, la scène rendue, puis entrer).
    if (requests.stop && current != Mode::Edit)
    {
        return Mode::Edit;
    }
    if (requests.playShortcut && current == Mode::Edit)
    {
        return Mode::Play;
    }
    if (requests.simulate && current == Mode::Edit)
    {
        return Mode::Simulate;
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
    case Mode::Simulate:
        return "Simuler";
    }
    return "";
}

/// Le bandeau de la barre de mode, une clé du catalogue : la pause, ou la souris rendue (Maj+F1,
/// que rien d'autre ne montre : sans lui, le jeu semble gelé), ou ce que le jeu fait à la scène.
/// Rien en Édition. La pause passe avant la souris rendue : elle arrête le jeu, la souris rendue
/// reste dite à la reprise. Simuler ne rend pas la souris (elle est déjà à l'éditeur : Maj+F1 y est
/// refusée, `playControlRefusal`) : son bandeau ne la dit jamais.
[[nodiscard]] constexpr const char* modeBannerOf(Mode mode, bool paused, bool mouseFreed)
{
    switch (mode)
    {
    case Mode::Edit:
        return "";
    case Mode::Play:
        if (paused)
        {
            return "Pause : la simulation est arrêtée";
        }
        return mouseFreed ? "Souris rendue : cliquer dans la scène pour la rendre au jeu"
                          : "Jouer : à l'arrêt, la scène revient telle qu'avant Play";
    case Mode::Simulate:
        return paused ? "Pause : la simulation est arrêtée"
                      : "Simuler : à l'arrêt, la scène revient telle qu'avant";
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
    case Mode::Simulate:
        return "simulate";
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
    case Action::CreateEntity:
    case Action::DeleteEntity:
    case Action::RenameEntity:
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
    NotInPlay,  ///< « seulement en Jouer » : Maj+F1, la souris étant déjà à l'éditeur en Simuler.
};

/// Ce qui empêche le contrôle. **« Image suivante » veut la pause** : en marche elle serait un pas
/// de plus que le temps ne mérite, et `planFrame` l'ignorerait de toute façon. **Maj+F1 veut
/// Jouer** : en Simuler le jeu n'a pas la souris, il n'y a rien à rendre ; la refuser bruyamment
/// (comptée, dite à la console) vaut mieux qu'un bandeau « Souris rendue » devant une souris qui
/// n'a jamais été au jeu.
[[nodiscard]] constexpr PlayRefusal playControlRefusal(PlayControl control, Mode mode, bool paused)
{
    switch (mode)
    {
    case Mode::Edit:
        return PlayRefusal::NotPlaying;
    case Mode::Play:
    case Mode::Simulate:
        if (control == PlayControl::FreeMouse && mode == Mode::Simulate)
        {
            return PlayRefusal::NotInPlay;
        }
        return control == PlayControl::StepFrame && !paused ? PlayRefusal::NotPaused
                                                            : PlayRefusal::None;
    }
    return PlayRefusal::NotPlaying;
}

/// Fait ce que le contrôle dit, supposé permis (`playControlRefusal`). Pause et Reprendre rendent
/// le jeu à l'immobile comme un changement de mode (`enterMode`) : ce qu'il tenait est relâché, les
/// appuis qu'aucun pas n'a vus sont oubliés. Une ligne de glu dans `withEditor`.
void applyPlayControl(PlayControl control, app::App& app, ModeState& state);

/// **La souris reste à l'éditeur quand le jeu ne reçoit rien** (Maj+F1, la pause, Simuler) : le jeu
/// repose `mouseCaptureWanted` à chaque image dans son `frame`, qui tourne encore avec la souris
/// rendue, pour un pas en pause (`programFrameRunsIn`) et en Simuler, et il capturerait la souris
/// de nouveau, ou effacerait le vol de l'éditeur. La capture vaut alors `editorWantsMouse` (le vol
/// en cours, `capturesMouse`). À appeler juste après le `frame`.
void keepMouseFromGame(app::App& app, const ModeState& state, bool editorWantsMouse);

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
/// la touche (`modeHintOf`). Le bandeau dit la pause ou la souris rendue (`modeBannerOf`), dans la
/// teinte du jeu (`playTintOf`).
void drawModeBar(const ModeState& mode, const app::ScreenRect& scene, const std::string& hint);

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
[[nodiscard]] ModeBarOverlay drawModeBarOverlay(const ModeState& mode, ImVec2 corner,
                                                const std::string& hint);

/// Alt+P est pressée, par la lettre que la table donne à « Jouer » (`imguiChordOf`, ADR-0036,
/// décision 13) : le raccourci d'Unreal pour jouer. ImGui doit recevoir le clavier : sous la route
/// *jeu*, il n'en a que les relâchements. Sans accord de lettre pour l'action, jamais.
[[nodiscard]] bool playShortcutPressed(const ShortcutTable& table);

/// Aucune fenêtre d'ImGui n'a le focus : la scène l'a, la fenêtre entière, panneaux fermés.
/// Panneaux ouverts, c'est la Vue qui a le focus ou non (`View::focused`, `sceneFocusedIn`).
[[nodiscard]] bool sceneHasFocus();

} // namespace levain::editor
