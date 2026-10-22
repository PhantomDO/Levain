#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
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

/// L'étiquette d'une entité dont le personnage est à (re)construire : son `CharacterController` a
/// changé. Comme les corps, au début du pas suivant (ADR-0028).
struct CharacterDirty
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

/// L'étiquette que `onEnter` et `onExit` posent sur leur observateur : ce sont les rappels du jeu,
/// que `endPlay` fait taire le temps de vider les relations `InsideOf`.
struct TriggerCallback
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

/// Le comparateur de flecs (`order_by`) qui range les entités dans l'ordre de leur **index**, les
/// 32 bits bas de l'identifiant. Jamais l'identifiant entier : la génération est dans les bits
/// hauts, et une entité réveillée (`ecs_make_alive`) passerait avant toutes les autres.
///
/// Les corps et les personnages se construisent, et les personnages avancent, dans cet ordre, non
/// dans celui des tables de flecs : l'ordre des tables dépend de l'histoire de chaque entité (une
/// étiquette posée puis retirée la renvoie en fin de table), celui des index non. Jolt n'est
/// déterministe que pour une même suite d'appels (ADR-0026, ADR-0037, décision 4) :
/// https://jrouwe.github.io/JoltPhysics/#deterministic-simulation
[[nodiscard]] int inIdOrder(flecs::entity_t first, const void* firstValue, flecs::entity_t second,
                            const void* secondValue);

/// L'index d'une entité : les 32 bits bas de son identifiant, sans la génération.
[[nodiscard]] std::uint32_t indexOfEntity(flecs::entity_t id);

/// Remplace le monde Jolt par un neuf : les identifiants de corps repartent de zéro, `OverlapState`
/// est vide. Le Play de l'éditeur l'appelle, pour que deux jeux d'une même scène partent d'un état
/// identique (ADR-0037, décision 4). Refusé, avec la raison au journal et en retour, si une entité
/// porte encore un `BodyHandle` ou un `CharacterHandle` : il désignerait un corps du monde qu'on
/// détruit. Rien n'a changé alors.
///
/// **Ce que l'appelant doit faire avant** : retirer ces handles (`scene::Transient` à Stop). Le
/// retrait suffit pour reconstruire : les observateurs `RecheckRefusedBody` et
/// `RecheckRefusedCharacter` ont un terme `without<Handle>`, que flecs rejoue en `OnRemove`, et
/// ils remettent `BodyDirty` et `CharacterDirty` ; le premier pas du jeu bâtit les corps dans le
/// monde neuf. Tester `has<BodyDirty>()` après le retrait (tests/physics_replay_test.cpp).
[[nodiscard]] std::optional<std::string> freshPhysicsWorld(flecs::world& world);

/// La fin du jeu, **en silence** : retire toutes les paires `(InsideOf, *)` et vide `OverlapState`
/// sans qu'aucun `onExit` du gameplay ne parte, et sans qu'aucun ne parte non plus pendant les
/// destructions qui suivent, puisqu'il ne reste plus de paire. Le Stop de l'éditeur l'appelle
/// d'abord, hors du mode différé (ADR-0037, décision 3). Seuls les rappels de `onEnter` et
/// `onExit` sont tus : un observateur que le gameplay poserait lui-même sur `InsideOf` entendrait
/// les retraits.
///
/// Refusé, avec la raison au journal et en retour, dans le mode différé, dans tous les builds :
/// flecs y mettrait la désactivation, les retraits et la réactivation en file, puis les rejouerait
/// ensemble, et les `onExit` partiraient. Rien n'a changé alors.
[[nodiscard]] std::optional<std::string> endPlay(flecs::world& world);

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
