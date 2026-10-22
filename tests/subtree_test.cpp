#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>
#include <flecs.h>

#include "levain/scene/components.hpp"
#include "levain/scene/reflection.hpp"
#include "levain/scene/scene.hpp"
#include "levain/scene/snapshot.hpp"
#include "levain/scene/subtree.hpp"
#include "levain/scene/tombstone.hpp"

// Un sous-arbre que la commande garde, supprime, puis rend (ADR-0038, décisions 3, 5, 10, morceau
// A2) : l'égalité se mesure par `differencesFrom` de M7.5, sur les entités du sous-arbre et sur
// toute la scène.

using levain::scene::Generated;
using levain::scene::readSubtree;
using levain::scene::removeKeepingIndex;
using levain::scene::sceneEntities;
using levain::scene::Snapshot;
using levain::scene::SubtreeRecord;
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

struct Unique
{
    std::unique_ptr<int> owned; // la copie est supprimée : readSubtree la refuse
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

flecs::entity_t deadEntity(const flecs::world& world)
{
    const flecs::entity entity = world.entity();
    const flecs::entity_t id = entity.id();
    entity.destruct();
    return id;
}

bool mentions(const std::string& text, std::string_view fragment)
{
    return text.find(fragment) != std::string::npos;
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

TEST_CASE("un enfant ChildOf n'est pas dans l'enregistrement : la pierre le rapporte")
{
    flecs::world world = makeWorld();
    const Scene s = makeScene(world);
    world.entity("observateur").child_of(s.cube);

    auto record = readSubtree(world, s.cube.id());
    REQUIRE(record.has_value());

    CHECK(record->entities.size() == 5);
    CHECK(record->entities[0].internalChildren.size() == 1);
    auto removed = removeKeepingIndex(world, s.cube.id());
    REQUIRE(removed.has_value());
    CHECK(removed->lost.size() == 1);
}

TEST_CASE(
    "readSubtree refuse, nommé et le monde intact : différé, mort, pierre, composant, généré, "
    "ne se copie pas")
{
    flecs::world world = makeWorld();
    const Scene s = makeScene(world);
    const flecs::entity generated = child(world, s.cube, "nœud").add<Generated>();
    const flecs::entity unique = world.entity("clé").set(Unique{});
    const flecs::entity_t deadId = deadEntity(world);

    world.defer_begin();
    const auto deferred = readSubtree(world, s.cube.id());
    world.defer_end();
    REQUIRE_FALSE(deferred.has_value());
    CHECK(mentions(deferred.error().message, "mode différé"));

    const auto dead = readSubtree(world, deadId);
    REQUIRE_FALSE(dead.has_value());
    CHECK(mentions(dead.error().message, "n'est pas vivante"));

    auto removed = removeKeepingIndex(world, s.b.id());
    REQUIRE(removed.has_value());
    const auto stone = readSubtree(world, removed->reserved[0].stone);
    REQUIRE_FALSE(stone.has_value());
    CHECK(mentions(stone.error().message, "pierre tombale"));

    const auto component = readSubtree(world, world.id<Mass>());
    REQUIRE_FALSE(component.has_value());
    CHECK(mentions(component.error().message, "n'est pas une entité de la scène"));

    const auto node = readSubtree(world, generated.id());
    REQUIRE_FALSE(node.has_value());
    CHECK(mentions(node.error().message, "nœud généré"));

    const auto uncopyable = readSubtree(world, unique.id());
    REQUIRE_FALSE(uncopyable.has_value());
    CHECK(mentions(uncopyable.error().message, "Unique"));
    CHECK(mentions(uncopyable.error().message, "clé"));
}

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

TEST_CASE("une entité faite à la main sous un nœud Generated est refusée, nommée, le monde intact")
{
    flecs::world world = makeWorld();
    const Scene s = makeScene(world);
    const flecs::entity bone = child(world, s.cube, "os").add<Generated>();
    const flecs::entity sword = child(world, bone, "épée"); // à la main, sous un nœud généré

    const auto read = readSubtree(world, s.cube.id());
    REQUIRE_FALSE(read.has_value());
    CHECK(mentions(read.error().message, "épée"));
    CHECK(mentions(read.error().message, "nœud généré"));
    const auto removal = removeKeepingIndex(world, s.cube.id());
    REQUIRE_FALSE(removal.has_value());
    CHECK(mentions(removal.error().message, "épée"));
    CHECK(world.is_alive(sword.id())); // rien n'a été détruit
    CHECK(world.is_alive(s.cube.id()));
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
