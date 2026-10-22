#include <algorithm>
#include <cstdint>
#include <vector>

#include <doctest/doctest.h>
#include <flecs.h>

#include "levain/scene/components.hpp"
#include "levain/scene/reflection.hpp"
#include "levain/scene/scene.hpp"
#include "levain/scene/snapshot.hpp"
#include "levain/scene/tombstone.hpp"

// Un sous-arbre que la commande garde, supprime, puis rend (ADR-0038, décisions 3, 5, 10, morceau
// A2) : l'égalité se mesure par `differencesFrom` de M7.5, sur les entités du sous-arbre et sur
// toute la scène.

using levain::scene::removeKeepingIndex;
using levain::scene::sceneEntities;
using levain::scene::Snapshot;
using levain::scene::takeSnapshot;

namespace
{

struct Mass
{
    float kilograms = 1.0F;
};

/// Une référence vers un frère : son identifiant doit rester vrai après le réveil.
struct Link
{
    flecs::entity target{};
};

struct Tag
{
};

flecs::world makeWorld()
{
    flecs::world world;
    REQUIRE(levain::scene::createIdRanges(world).has_value());
    world.import<levain::scene::SceneModule>();
    levain::scene::describeAuthored<Mass>(world);
    levain::scene::describeAuthored<Link>(world);
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

/// La scène du critère : « Haut » porte A, Cube et B ; Cube porte trois enfants, dont le deuxième
/// porte un petit-enfant ; l'ordre des enfants de Cube n'est pas l'ordre de création ; k1 désigne
/// k3 (une référence vers un frère).
struct Scene
{
    flecs::entity top;
    flecs::entity a;
    flecs::entity cube;
    flecs::entity b;
    flecs::entity k1;
    flecs::entity k2;
    flecs::entity k3;
    flecs::entity grandchild;
};

Scene makeScene(flecs::world& world)
{
    Scene s;
    s.top = world.entity("Haut").set(Mass{9.0F});
    s.a = child(world, s.top, "A");
    s.cube = child(world, s.top, "Cube").set(Mass{2.0F}).add<Tag>();
    s.b = child(world, s.top, "B");
    s.k1 = child(world, s.cube, "k1").set(Mass{3.0F});
    s.k2 = child(world, s.cube, "k2").set(Mass{4.0F});
    s.k3 = child(world, s.cube, "k3");
    s.grandchild = child(world, s.k2, "petit-enfant").set(Mass{5.0F}).add<Tag>();
    s.k1.set(Link{.target = s.k3});
    const std::vector<flecs::entity_t> order{s.k3.id(), s.k1.id(), s.k2.id()};
    ecs_set_child_order(world, s.cube, order.data(), static_cast<std::int32_t>(order.size()));
    // La place de Cube parmi ses frères : au milieu, pas à la fin.
    const std::vector<flecs::entity_t> siblings{s.a.id(), s.cube.id(), s.b.id()};
    ecs_set_child_order(world, s.top, siblings.data(), static_cast<std::int32_t>(siblings.size()));
    return s;
}

} // namespace

TEST_CASE(
    "une pierre vivante n'est pas une entité de la scène : elle n'entre ni dans sceneEntities "
    "ni dans l'instantané")
{
    flecs::world world = makeWorld();
    const Scene s = makeScene(world);
    const std::size_t sceneSize = sceneEntities(world).size();
    auto removed = removeKeepingIndex(world, s.cube.id());
    REQUIRE(removed.has_value());
    REQUIRE(removed->reserved.size() == 5);

    for (const auto& reserved : removed->reserved)
    {
        CHECK_FALSE(levain::scene::isSceneEntity(world, reserved.stone));
    }
    CHECK(sceneEntities(world).size() == sceneSize - 5);
    const Snapshot snapshot = snapshotOf(world);
    CHECK(snapshot.entities.size() == sceneSize - 5);
    for (const auto& reserved : removed->reserved)
    {
        CHECK(std::ranges::none_of(snapshot.entities,
                                   [&](const auto& record)
                                   {
                                       return levain::scene::entityIndexOf(record.id) ==
                                              levain::scene::entityIndexOf(reserved.stone);
                                   }));
    }
}

TEST_CASE("isSceneEntity ne crée aucune entité quand le module de la scène est importé")
{
    flecs::world world;
    world.import<levain::scene::SceneModule>();
    const flecs::entity entity = world.entity("une entité");
    const std::int32_t before = ecs_get_entities(world).alive_count;

    CHECK(levain::scene::isSceneEntity(world, entity.id()));
    CHECK(levain::scene::sceneEntities(world).size() == 1);

    CHECK(ecs_get_entities(world).alive_count == before);
}
