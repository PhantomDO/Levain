#pragma once

#include <flecs.h>

#include "levain/scene/fixed_step.hpp"

namespace levain::scene
{

/// Ce qui fait d'une entité une phase du pipeline de simulation. Pas `flecs::Phase` : le pipeline
/// par défaut prend toute entité qui le porte, et rejouerait la simulation une fois de plus par
/// image, avec le temps réel (ADR-0026, vu au prototype).
struct SimulationPhase
{
};

/// La première phase de simulation, celle du gameplay : ses systèmes tournent dans un pipeline à
/// part, exécuté N fois par image avec un pas fixe (ADR-0016). Un système de gameplay se déclare
/// `world.system<...>().kind<levain::scene::Simulation>()`, et son `delta_time` vaut alors toujours
/// un pas — c'est ce qui rend son résultat reproductible.
struct Simulation
{
};

/// La phase du pas de physique, après le gameplay (ADR-0026) : ce que le gameplay demande à la
/// physique pendant un pas est pris en compte dans ce même pas, comme `FixedUpdate` avant la
/// simulation chez Unity. Le module `physics` y range ses systèmes.
struct Physics
{
};

/// Après le pas de physique : ce qui lit son résultat (les volumes déclencheurs, le personnage).
struct PostPhysics
{
};

/// Le pipeline des systèmes de simulation, posé en singleton par le module pour qu'`advanceWorld`
/// le retrouve.
struct SimulationPipeline
{
    flecs::entity_t pipeline = 0;
};

/// Le module flecs de la scène : ses composants et ses systèmes, rangés par phase du pipeline de
/// flecs. S'installe par `world.import<levain::scene::SceneModule>()`.
///
/// Les modules de flecs : manuel, section « Modules »
/// (https://www.flecs.dev/flecs/md_docs_2Manual.html).
struct SceneModule
{
    explicit SceneModule(flecs::world& world);
};

/// Avance le monde d'une image : les pas de simulation que `frameSeconds` a mérités, puis une passe
/// de rendu (interpolation et matrices monde). Renvoie le nombre de pas exécutés.
///
/// La glu entre l'horloge de l'application et les deux pipelines de flecs ; le calcul, lui, est
/// dans `planSteps` (`fixed_step.hpp`), qui se teste sans monde.
int advanceWorld(flecs::world& world, FixedStep& step, float frameSeconds);

} // namespace levain::scene
