#include <doctest/doctest.h>
#include <flecs.h>

#include "levain/scene/components.hpp"
#include "levain/scene/reflection.hpp"
#include "levain/scene/scene.hpp"
#include "levain/scene/snapshot.hpp"

// L'instantané de la scène (ADR-0037, morceau 1) : ce qu'il garde, ce qu'il refuse, et chaque sorte
// de différence que `differencesFrom` doit trouver. Sans éditeur ni GPU.

using levain::scene::isSceneEntity;
using levain::scene::Transform;

namespace
{

struct Mass
{
    float kilograms = 1.0f;
};

flecs::world makeWorld()
{
    flecs::world world;
    world.import<levain::scene::SceneModule>();
    levain::scene::describeAuthored<Mass>(world);
    return world;
}

flecs::entity child(const flecs::world& world, flecs::entity parent, const char* name)
{
    return world.entity().set(flecs::Parent{parent}).set_name(name);
}

} // namespace

TEST_CASE("isSceneEntity dit oui à une entité placée et à l'enfant d'un Parent")
{
    flecs::world world = makeWorld();
    const flecs::entity root = world.entity("racine").set(Transform{});
    const flecs::entity kid = child(world, root, "enfant");
    const flecs::entity bare = world.entity();

    CHECK(isSceneEntity(world, root));
    // Un enfant de `flecs::Parent` répond oui à has(ChildOf, *) : c'est la TABLE qu'on lit.
    CHECK(kid.has(flecs::ChildOf, flecs::Wildcard));
    CHECK(isSceneEntity(world, kid));
    CHECK(isSceneEntity(world, bare));
}

TEST_CASE("isSceneEntity dit non à ce que flecs range et à ce qui est mort")
{
    flecs::world world = makeWorld();
    const flecs::entity module = world.import<levain::scene::SceneModule>();
    const flecs::entity system = world.system("système").run([](flecs::iter&) {});
    const flecs::entity observer =
        world.observer<Transform>("observateur").event(flecs::OnSet).each([](Transform&) {});
    const flecs::entity query =
        world.entity(world.query_builder<Transform>("requête").build().entity());
    const flecs::entity component = world.component<Mass>();
    const flecs::entity prefab = world.prefab("modèle");
    const flecs::entity underPath = world.entity("a::b"); // rangée par ChildOf sous « a »
    const flecs::entity dead = world.entity();
    const flecs::entity_t deadId = dead.id();
    dead.destruct();

    CHECK_FALSE(isSceneEntity(world, module));
    CHECK_FALSE(isSceneEntity(world, system));
    CHECK_FALSE(isSceneEntity(world, observer));
    CHECK_FALSE(isSceneEntity(world, query));
    CHECK_FALSE(isSceneEntity(world, component));
    CHECK_FALSE(isSceneEntity(world, prefab));
    CHECK_FALSE(isSceneEntity(world, underPath));
    CHECK_FALSE(isSceneEntity(world, flecs::Module)); // flecs lui-même, sous EcsFirstUserEntityId
    CHECK_FALSE(isSceneEntity(world, deadId));
}
