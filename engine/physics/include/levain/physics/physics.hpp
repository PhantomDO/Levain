#pragma once

#include <functional>
#include <vector>

#include <flecs.h>

#include "levain/physics/physics_world.hpp"

namespace levain::physics
{

/// L'étiquette d'une entité dont le corps est à (re)construire : son `Collider` ou son `RigidBody`
/// a changé. Les corps se construisent au début du pas de physique suivant, tous ensemble.
struct BodyDirty
{
};

/// La relation qu'un volume déclencheur pose sur chaque corps qu'il contient : `(InsideOf, volume)`
/// (ADR-0027). Rangée sur le corps et non sur le volume : dans un ECS à archetypes, la liste
/// changeante des occupants ferait du volume une table nouvelle à chaque entrée. flecs indexe une
/// paire par sa cible : chercher ce qui est dans un volume coûte autant que si c'était rangé sur
/// lui.
///
/// Le gameplay passe d'ordinaire par l'API du volume, plus bas ; le corps peut aussi demander
/// `player.has<InsideOf>(lake)`. La relation appartient au module : la retirer à la main le
/// désaccorde, et il ne la reposera pas tant que le corps reste dans le volume.
struct InsideOf
{
};

/// Ce qui était dans les volumes au pas précédent, pour n'appliquer que ce qui a changé.
struct OverlapState
{
    std::vector<Overlap> current;
    std::vector<Overlap> next;
    std::vector<Overlap> changed; ///< Gardé d'un pas à l'autre : la différence n'alloue pas.
};

/// La liste des corps déplacés par le dernier pas, gardée d'un pas à l'autre pour ne pas allouer.
struct MovedBodyBuffer
{
    std::vector<MovedBody> bodies;
};

/// Le module flecs de la physique (ADR-0026) : le monde Jolt en singleton, les corps qui suivent
/// les entités, et le pas de physique dans la phase `scene::Physics` du pipeline de simulation.
///
/// Les réglages se changent en posant un `PhysicsSettings` en singleton **avant** l'import :
/// `world.set<PhysicsSettings>({...}); world.import<levain::physics::PhysicsModule>();`.
struct PhysicsModule
{
    explicit PhysicsModule(flecs::world& world);
};

/// Appelle `react` pour chaque corps qui **entre** dans `volume`, dans la phase `PostPhysics` du
/// pas où il entre : le `BeginOverlap` d'Unreal, l'`OnTriggerEnter` d'Unity, le `body_entered` de
/// Godot.
///
///     // Un piège blesse ce qui entre.
///     levain::physics::onEnter(world, trap, [](flecs::entity body) { hurt(body); });
///
/// Rend l'observateur flecs : le détruire (`destruct`) arrête de réagir. Il est un enfant du volume
/// et part avec lui : flecs refuse de supprimer une entité qu'un observateur vise encore.
flecs::observer onEnter(flecs::world& world, flecs::entity volume,
                        std::function<void(flecs::entity body)> react);

/// Appelle `react` pour chaque corps qui **sort** de `volume`, ou dont l'entité est supprimée en
/// lui : le corps est alors encore vivant, avec tous ses composants (flecs émet `OnRemove` avant
/// de le détruire), et rien ne distingue les deux cas. Le volume supprimé, ses `onEnter` et
/// `onExit` partent avec lui, et peuvent voir ou non cette dernière sortie (flecs traite les tables
/// dans leur ordre de création) : ne pas s'y fier. Les corps, eux, perdent bien leur `InsideOf`.
///
///     levain::physics::onExit(world, lake, [](flecs::entity body) { body.remove<Swimming>(); });
flecs::observer onExit(flecs::world& world, flecs::entity volume,
                       std::function<void(flecs::entity body)> react);

/// Les corps qui sont dans `volume` maintenant, à la fin du dernier pas.
///
///     for (const flecs::entity body : levain::physics::occupantsOf(world, lake)) { … }
///
/// Une requête construite à chaque appel : pour un parcours à chaque pas, un système est moins
/// cher, et parallélisable, puisque ce sont des données. Il vise les lacs par une variable : un
/// système qui nommerait ce lac-ci empêcherait de le supprimer (flecs refuse de supprimer une
/// entité qu'une requête vise encore).
///
///     world.system<Stamina>()
///         .with<levain::physics::InsideOf>("$volume")
///         .with<Lake>().src("$volume")
///         .kind<levain::scene::Simulation>()
///         .multi_threaded()
///         .each([](Stamina& stamina) { stamina.value -= drain; });
[[nodiscard]] std::vector<flecs::entity> occupantsOf(const flecs::world& world,
                                                     flecs::entity volume);

} // namespace levain::physics
