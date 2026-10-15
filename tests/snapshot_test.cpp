#include <algorithm>
#include <memory>
#include <vector>

#include <doctest/doctest.h>
#include <flecs.h>

#include "levain/physics/physics.hpp"
#include "levain/scene/components.hpp"
#include "levain/scene/reflection.hpp"
#include "levain/scene/scene.hpp"
#include "levain/scene/snapshot.hpp"

// L'instantané de la scène (ADR-0037, morceau 1) : ce qu'il garde, ce qu'il refuse, et chaque sorte
// de différence que `differencesFrom` doit trouver. Sans éditeur ni GPU.

using levain::scene::isSceneEntity;
using levain::scene::Snapshot;
using levain::scene::takeSnapshot;
using levain::scene::Transform;

namespace
{

struct Mass
{
    float kilograms = 1.0f;
};

struct Tag
{
};

struct Likes
{
};

/// Un composant qui ne se copie pas : flecs le range en `ECS_TYPE_HOOK_COPY_ILLEGAL`.
struct Unique
{
    std::unique_ptr<int> owned; // pas trivialement copiable, et sa copie est supprimée
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

Snapshot snapshotOf(const flecs::world& world)
{
    auto snapshot = takeSnapshot(world);
    REQUIRE(snapshot.has_value());
    return std::move(*snapshot);
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

TEST_CASE("l'instantané d'un monde de modules est vide")
{
    flecs::world world = makeWorld();
    world.import<levain::physics::PhysicsModule>();

    CHECK(snapshotOf(world).entities.empty());
}

TEST_CASE(
    "l'instantané garde l'identifiant, le nom, le parent, le type, les valeurs et les enfants")
{
    flecs::world world = makeWorld();
    const flecs::entity root = world.entity("racine").set(Mass{.kilograms = 3.0f});
    const flecs::entity first = child(world, root, "premier").add<Tag>().add<Likes>(root);
    const flecs::entity second = child(world, root, "second");
    // Un observateur rangé sous son volume par ChildOf : un enfant interne.
    const flecs::entity watcher =
        world.observer<Mass>("guet").event(flecs::OnSet).each([](Mass&) {}).child_of(root);
    // Un identifiant de génération 1 : l'instantané le garde en entier.
    const flecs::entity recycled = world.entity("recyclée");
    const flecs::entity_t oldId = recycled.id();
    recycled.destruct();
    const flecs::entity reborn = world.entity("revenue");
    REQUIRE(reborn.id() != oldId);

    const Snapshot snapshot = snapshotOf(world);
    const auto record = [&](flecs::entity entity)
    { return std::ranges::find(snapshot.entities, entity.id(), &levain::scene::EntityRecord::id); };

    REQUIRE(record(root) != snapshot.entities.end());
    CHECK(record(root)->name == "racine");
    CHECK(record(root)->parent == 0);
    CHECK(record(root)->children == std::vector<flecs::entity_t>{first.id(), second.id()});
    CHECK(record(root)->internalChildren == std::vector<flecs::entity_t>{watcher.id()});
    REQUIRE(record(root)->values.size() == 1);
    CHECK(static_cast<const Mass*>(record(root)->values[0].value.data())->kilograms == 3.0f);
    REQUIRE(record(first) != snapshot.entities.end());
    CHECK(record(first)->parent == root.id());
    CHECK(record(first)->type.size() == 2); // Tag et (Likes, racine) ; ni nom, ni Parent
    CHECK((reborn.id() >> 32U) != 0);
    CHECK(record(reborn) != snapshot.entities.end());
    CHECK(record(watcher) == snapshot.entities.end()); // un observateur n'est pas de la scène
}

TEST_CASE("un composant Transient n'est ni copié ni refusé")
{
    flecs::world world = makeWorld();
    world.component<Unique>().add<levain::scene::Transient>();
    world.component<Mass>(); // décrit plus haut
    const flecs::entity owner = world.entity("porteuse").set(Unique{}).set(Mass{});

    const Snapshot snapshot = snapshotOf(world);

    REQUIRE(snapshot.entities.size() == 1);
    CHECK(snapshot.entities[0].id == owner.id());
    CHECK(snapshot.entities[0].type.size() == 1); // Mass seul
    CHECK(snapshot.entities[0].values.size() == 1);
}
