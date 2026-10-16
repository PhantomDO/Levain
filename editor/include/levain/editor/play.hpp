#pragma once

// Jouer et Arrêter (ADR-0037) : Play garde la scène en mémoire, Stop la rend en place, avec les
// mêmes identifiants. Ce fichier ne fait que l'ordre des étapes et ce que Play garde en dehors du
// monde (l'accumulateur du pas fixe, l'animation, les assets en usage) ; la copie et la
// restauration de la scène sont dans `scene/snapshot.hpp`, la physique dans `physics.hpp`.
// Les fonctions sont libres et reçoivent leurs dépendances (ADR-0011) ; `changeMode` n'en a que la
// glu. Équivalents : le Play d'Unity sans rechargement de scène (« only resets the scene's
// modified contents ») ; Unreal joue dans une copie du monde (PIE) ; Godot dans un autre processus.

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <flecs.h>

#include "levain/app/app.hpp"
#include "levain/app/models.hpp"
#include "levain/core/error.hpp"
#include "levain/scene/fixed_step.hpp"
#include "levain/scene/snapshot.hpp"

namespace levain::editor
{

/// Ce que Play et Stop touchent de l'`App`, un champ par dépendance : les fonctions ci-dessous ne
/// lisent rien d'autre, un test les appelle sans fenêtre ni GPU.
struct PlayTarget
{
    flecs::world& world;
    const scene::IdRanges& ranges;
    scene::FixedStep& fixedStep;
    std::map<assets::AssetId, app::ModelGpu>& models;
    app::AnimationClock& animationClock;
    int stepsPlayed; ///< `App::stepsPlayed` : de quoi compter les pas de cette session.
};

/// La glu : les champs d'`App` que Play et Stop touchent.
[[nodiscard]] inline PlayTarget playTargetOf(app::App& app)
{
    return {app.world,      app.idRanges, app.fixedStep, app.models, app.skinningState.clock,
            app.stepsPlayed};
}

/// Tout ce que Play garde pour que Stop le rende. **Vit tant que le jeu dure** : un instantané ne
/// survit pas à son monde (`scene::ValueBox`).
struct PlaySession
{
    scene::Snapshot snapshot;
    scene::FixedStep
        fixedStep; ///< L'accumulateur : en Édition il ne reçoit pas le temps des images.
    app::AnimationMemo animation;
    int stepsAtStart = 0;
};

/// startPlay : l'instantané de la scène, les assets retenus, un monde Jolt neuf, et les nouveaux
/// identifiants pris dans la plage du jeu. **Chaque refus laisse le monde comme il était** et rend
/// la raison (la console la dit) : un composant qui ne se copie pas, des assets déjà retenus, un
/// handle de Jolt resté posé. À appeler hors du mode différé, en Édition.
[[nodiscard]] core::Result<PlaySession> startPlay(const PlayTarget& target);

/// Ce que Stop a fait, pour la console et pour la ligne de la CI.
struct StopReport
{
    /// Stop n'a pas eu lieu (le monde est en mode différé) : on reste en jeu, rien n'a changé.
    bool refused = false;
    std::string refusal;
    int steps = 0; ///< Les pas joués pendant cette session.
    /// Le mode que Stop quitte, en mot sans langue (`modeTokenOf` : « play », « simulate »), que
    /// l'appelant donne à `stopPlay` : la ligne de la CI le dit (`from=`), sans quoi un Alt+S qui
    /// jouerait à la place de simuler rendrait la même ligne. Posé aussi quand Stop est refusé.
    std::string from;
    /// Le nombre de vraies différences que `scene::compareWithSnapshot` a trouvées : 0 si la scène
    /// est identique, sans plafond, enfants internes perdus exclus (ils sont dans
    /// `lostInternalChildren`). C'est la valeur de `differences=` dans la ligne de la CI.
    std::size_t differences = 0;
    /// Les différences nommées (les premières), vide si la scène est identique.
    std::string differencesText;
    /// Pourquoi `restoreSnapshot` a échoué, vide s'il a réussi : dit à la console même quand aucune
    /// différence n'est comptée (une garde qui refuse, le monde intact).
    std::string restoreError;
    /// Un enfant interne détruit en jeu (l'observateur d'un `onEnter`) ne revient pas.
    std::vector<std::string> lostInternalChildren;
    /// Ce que Stop n'a pu rendre à l'Édition (le monde Jolt neuf refusé), pour la console.
    std::vector<std::string> problems;
    double milliseconds = 0.0;
};

/// stopPlay : dans cet ordre, la physique finit le jeu (`physics::endPlay`, avant toute
/// destruction : aucun `onExit` du jeu ne doit partir), la scène est rendue (`restoreSnapshot`),
/// les identifiants reviennent à l'édition, le monde Jolt est remplacé (sans corps, comme avant le
/// premier Play), puis l'accumulateur, l'animation et les assets retenus sont rendus. **Hors du
/// mode différé.** Une différence trouvée n'empêche pas l'Édition de reprendre (elle est dans le
/// rapport) ; seul un refus (`refused`) laisse le jeu en cours. `from` est le mode quitté
/// (`modeTokenOf`), recopié dans le rapport.
[[nodiscard]] StopReport stopPlay(const PlayTarget& target, PlaySession& session,
                                  std::string_view from);

/// La ligne de la CI, sous son nom stable (ADR-0036, décision 14) : `editor.stop steps=120
/// differences=0 ms=3.2 from=simulate -- Stop : …`. Des mots sans langue, puis la phrase.
[[nodiscard]] std::string stopLineOf(const StopReport& report);

/// Écrit dans la console ce que Stop a trouvé : un avertissement par enfant interne perdu, une
/// erreur qui nomme les premières différences, un problème par erreur de remise en état, puis la
/// ligne de la CI.
void logStop(const StopReport& report);

} // namespace levain::editor
