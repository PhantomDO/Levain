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

using levain::scene::checkSubtreeRestorable;
using levain::scene::checkTombstonesHold;
using levain::scene::differencesFrom;
using levain::scene::Generated;
using levain::scene::readSubtree;
using levain::scene::removeKeepingIndex;
using levain::scene::restoreSubtree;
using levain::scene::reviveFromTombstones;
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

/// Ce qu'un éditeur crée entre la suppression et l'annulation.
void createBystanders(flecs::world& world)
{
    for (int i = 0; i < 8; ++i)
    {
        world.entity().set(Mass{});
    }
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

TEST_CASE("un sous-arbre supprimé puis rendu est égal à celui d'avant, ordre des frères compris")
{
    flecs::world world = makeWorld();
    const Scene s = makeScene(world);
    const Snapshot before = snapshotOf(world);

    auto record = readSubtree(world, s.cube.id());
    REQUIRE(record.has_value());
    REQUIRE(record->entities.size() == 5);        // Cube, k3, k1, k2, petit-enfant
    CHECK(record->entities[0].id == s.cube.id()); // la racine d'abord
    CHECK(record->parent == s.top.id());
    CHECK(record->parentChildren == std::vector<flecs::entity_t>{s.a.id(), s.cube.id(), s.b.id()});
    auto removed = removeKeepingIndex(world, s.cube.id());
    REQUIRE(removed.has_value());
    CHECK_FALSE(ecs_is_alive(world, s.k2.id()));
    createBystanders(world);
    const auto afterRemoval = differencesFrom(world, before);
    CHECK_FALSE(afterRemoval.empty()); // la scène a bien changé entre-temps

    REQUIRE(reviveFromTombstones(world, *removed).has_value());
    REQUIRE(restoreSubtree(world, *record).has_value());

    // Les créations de l'entre-deux sont en trop pour l'instantané : on ne regarde que le
    // sous-arbre.
    const std::vector<std::string> differences = differencesFrom(world, before);
    for (const std::string& line : differences)
    {
        CHECK(mentions(line, "entité de la scène en trop"));
    }
    CHECK(world.is_alive(s.grandchild.id()));
    CHECK(s.grandchild.get<Mass>().kilograms == 5.0F);
    CHECK(s.k1.get<Link>().target.id() == s.k3.id());
    CHECK(std::string{s.k2.path().c_str()} == "::Haut::Cube::k2");
    CHECK(world.lookup("Haut::Cube::k2").id() == s.k2.id());
    CHECK(levain::scene::orderedChildrenOf(world, s.cube) ==
          std::vector<flecs::entity_t>{s.k3.id(), s.k1.id(), s.k2.id()});
    CHECK(levain::scene::orderedChildrenOf(world, s.top) ==
          std::vector<flecs::entity_t>{s.a.id(), s.cube.id(), s.b.id()});
}

TEST_CASE("sans créations entre-temps, la scène entière est égale à l'instantané")
{
    flecs::world world = makeWorld();
    const Scene s = makeScene(world);
    const Snapshot before = snapshotOf(world);

    auto record = readSubtree(world, s.top.id());
    REQUIRE(record.has_value());
    auto removed = removeKeepingIndex(world, s.top.id());
    REQUIRE(removed.has_value());
    CHECK(sceneEntities(world).empty()); // les pierres ne sont pas la scène
    REQUIRE(reviveFromTombstones(world, *removed).has_value());
    REQUIRE(restoreSubtree(world, *record).has_value());

    CHECK(differencesFrom(world, before).empty());
}

TEST_CASE("un nœud Generated n'est ni dans l'enregistrement ni sous une pierre, et ne revient pas")
{
    flecs::world world = makeWorld();
    const Scene s = makeScene(world);
    const flecs::entity node = child(world, s.cube, "nœud").add<Generated>();
    const flecs::entity below = child(world, node, "sous le nœud").add<Generated>();
    const flecs::entity_t nodeId = node.id();
    const flecs::entity_t belowId = below.id();

    auto record = readSubtree(world, s.cube.id());
    REQUIRE(record.has_value());
    CHECK(record->entities.size() == 5); // le nœud et ce qu'il porte n'y sont pas
    for (const auto& entity : record->entities)
    {
        CHECK(entity.id != nodeId);
        CHECK(entity.id != belowId);
        CHECK(std::ranges::find(entity.children, nodeId) == entity.children.end());
    }
    auto removed = removeKeepingIndex(world, s.cube.id());
    REQUIRE(removed.has_value());
    CHECK(removed->reserved.size() == 5); // pas de pierre pour le nœud : il serait revenu vide
    REQUIRE(reviveFromTombstones(world, *removed).has_value());
    REQUIRE(restoreSubtree(world, *record).has_value());

    CHECK_FALSE(world.is_alive(nodeId));
    CHECK_FALSE(world.is_alive(belowId));
    CHECK(sceneEntities(world).size() == 8); // Haut, A, B, Cube, k1, k2, k3, petit-enfant
}

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

TEST_CASE("restoreSubtree refuse, nommé et le monde intact : différé, entité non réveillée, "
          "parent mort")
{
    flecs::world world = makeWorld();
    const Scene s = makeScene(world);
    auto record = readSubtree(world, s.cube.id());
    REQUIRE(record.has_value());

    world.defer_begin();
    const auto deferred = restoreSubtree(world, *record);
    world.defer_end();
    REQUIRE_FALSE(deferred.has_value());
    CHECK(mentions(deferred.error().message, "mode différé"));

    auto removed = removeKeepingIndex(world, s.cube.id());
    REQUIRE(removed.has_value());
    const auto notRevived = restoreSubtree(world, *record); // le réveil n'a pas eu lieu
    REQUIRE_FALSE(notRevived.has_value());
    CHECK(mentions(notRevived.error().message, "pas vivante"));

    REQUIRE(reviveFromTombstones(world, *removed).has_value());
    auto stale = readSubtree(world, s.cube.id());
    REQUIRE(stale.has_value());
    stale->parent = deadEntity(world); // le parent a disparu depuis la lecture
    const auto orphan = restoreSubtree(world, *stale);
    REQUIRE_FALSE(orphan.has_value());
    CHECK(mentions(orphan.error().message, "parent"));
    CHECK(mentions(orphan.error().message, "est mort"));
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

TEST_CASE(
    "les nœuds Generated gardent leur place de repère : supprimer le dernier frère et annuler "
    "le remet après eux")
{
    flecs::world world = makeWorld();
    const flecs::entity top = world.entity("Haut");
    const flecs::entity g1 = child(world, top, "G1").add<Generated>();
    const flecs::entity g2 = child(world, top, "G2").add<Generated>();
    const flecs::entity lamp = child(world, top, "Lampe");
    const std::vector<flecs::entity_t> before{g1.id(), g2.id(), lamp.id()};

    auto record = readSubtree(world, lamp.id());
    REQUIRE(record.has_value());
    auto removed = removeKeepingIndex(world, lamp.id());
    REQUIRE(removed.has_value());
    REQUIRE(reviveFromTombstones(world, *removed).has_value());
    REQUIRE(restoreSubtree(world, *record).has_value());

    CHECK(levain::scene::orderedChildrenOf(world, top) == before);
}

TEST_CASE("un frère apparu entre-temps garde sa place : la racine revient après son prédécesseur")
{
    flecs::world world = makeWorld();
    const flecs::entity top = world.entity("Haut");
    const flecs::entity a = child(world, top, "A");
    const flecs::entity r = child(world, top, "R");
    const flecs::entity b = child(world, top, "B");

    auto record = readSubtree(world, r.id());
    REQUIRE(record.has_value());
    auto removed = removeKeepingIndex(world, r.id());
    REQUIRE(removed.has_value());
    const flecs::entity c = child(world, top, "C"); // hors de la pile, entre A et B
    const std::vector<flecs::entity_t> meanwhile{a.id(), c.id(), b.id()};
    ecs_set_child_order(world, top, meanwhile.data(), static_cast<std::int32_t>(meanwhile.size()));
    REQUIRE(reviveFromTombstones(world, *removed).has_value());
    REQUIRE(restoreSubtree(world, *record).has_value());

    CHECK(levain::scene::orderedChildrenOf(world, top) ==
          std::vector<flecs::entity_t>{a.id(), r.id(), c.id(), b.id()});
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

TEST_CASE("l'enregistrement et les pierres ont les mêmes membres : un préfab sous flecs::Parent "
          "n'est pas réservé, il est rapporté")
{
    flecs::world world = makeWorld();
    const Scene s = makeScene(world);
    const flecs::entity prefab = world.prefab("modèle").set(flecs::Parent{s.cube});
    const std::size_t sceneSize = sceneEntities(world).size();

    auto record = readSubtree(world, s.cube.id());
    REQUIRE(record.has_value());
    auto removed = removeKeepingIndex(world, s.cube.id());
    REQUIRE(removed.has_value());

    std::vector<flecs::entity_t> recorded;
    for (const auto& entity : record->entities)
    {
        recorded.push_back(entity.id);
    }
    std::vector<flecs::entity_t> reserved;
    for (const auto& index : removed->reserved)
    {
        reserved.push_back(index.entity);
    }
    CHECK(recorded == reserved);
    REQUIRE(removed->lost.size() == 1);
    CHECK(removed->lost[0].id == prefab.id());
    REQUIRE(reviveFromTombstones(world, *removed).has_value());
    REQUIRE(restoreSubtree(world, *record).has_value());
    CHECK(sceneEntities(world).size() == sceneSize); // pas de racine fantôme vide
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

TEST_CASE(
    "une racine dont le parent est un nœud Generated est refusée, nommée, le monde intact (Q2)")
{
    flecs::world world = makeWorld();
    const Scene s = makeScene(world);
    const flecs::entity bone = child(world, s.cube, "os").add<Generated>();
    const flecs::entity sword = child(world, bone, "épée");

    const auto read = readSubtree(world, sword.id());
    REQUIRE_FALSE(read.has_value());
    CHECK(mentions(read.error().message, "épée"));
    CHECK(mentions(read.error().message, "nœud généré"));
    const auto removal = removeKeepingIndex(world, sword.id());
    REQUIRE_FALSE(removal.has_value());
    CHECK(mentions(removal.error().message, "nœud généré"));
    CHECK(world.is_alive(sword.id())); // rien n'a été détruit
}

TEST_CASE("checkSubtreeRestorable refuse un parent mort avant le réveil : la pierre tient, "
          "aucune entité vide n'est laissée (Q2)")
{
    flecs::world world = makeWorld();
    const flecs::entity top = world.entity("Haut");
    const flecs::entity sword = child(world, top, "épée").set(Mass{3.0F});
    auto record = readSubtree(world, sword.id());
    REQUIRE(record.has_value());
    auto removed = removeKeepingIndex(world, sword.id());
    REQUIRE(removed.has_value());
    REQUIRE(checkTombstonesHold(world, *removed).has_value());
    REQUIRE(checkSubtreeRestorable(world, *record).has_value()); // le parent vit : permis

    top.destruct(); // le parent disparaît entre la suppression et l'annulation
    const std::size_t sceneSize = sceneEntities(world).size();
    const auto refused = checkSubtreeRestorable(world, *record);

    REQUIRE_FALSE(refused.has_value());
    CHECK(mentions(refused.error().message, "est mort"));
    CHECK(checkTombstonesHold(world, *removed).has_value()); // la pierre tient toujours
    CHECK(sceneEntities(world).size() == sceneSize);         // rien n'a été réveillé
}

TEST_CASE(
    "un enfant ChildOf d'un nœud Generated n'est pas rapporté perdu : le générateur le refait")
{
    flecs::world world = makeWorld();
    const Scene s = makeScene(world);
    const flecs::entity node = child(world, s.cube, "nœud").add<Generated>();
    const flecs::entity inner = world.entity("rappel du générateur").child_of(node);
    world.entity("observateur").child_of(s.cube); // celui-ci, sous la scène, l'est

    auto removed = removeKeepingIndex(world, s.cube.id());

    REQUIRE(removed.has_value());
    REQUIRE(removed->lost.size() == 1);
    CHECK(removed->lost[0].name == "observateur");
    CHECK_FALSE(world.is_alive(inner.id())); // détruit quand même avec le nœud
}

TEST_CASE("des milliers de frères générés refaits : la racine revient à sa place")
{
    flecs::world world = makeWorld();
    const flecs::entity top = world.entity("Haut");
    const flecs::entity first = child(world, top, "Premier");
    constexpr int Siblings = 3000;
    std::vector<flecs::entity_t> generated;
    generated.reserve(Siblings);
    for (int i = 0; i < Siblings; ++i)
    {
        generated.push_back(child(world, top, "g").add<Generated>().id());
    }
    const flecs::entity last = child(world, top, "Dernier");
    auto record = readSubtree(world, last.id());
    REQUIRE(record.has_value());
    auto removed = removeKeepingIndex(world, last.id());
    REQUIRE(removed.has_value());
    REQUIRE(checkTombstonesHold(world, *removed).has_value());
    REQUIRE(reviveFromTombstones(world, *removed).has_value());
    REQUIRE(restoreSubtree(world, *record).has_value());

    std::vector<flecs::entity_t> expected{first.id()};
    expected.insert(expected.end(), generated.begin(), generated.end());
    expected.push_back(last.id());
    CHECK(levain::scene::orderedChildrenOf(world, top) == expected);
}
