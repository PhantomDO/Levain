#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
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

using levain::scene::differencesFrom;
using levain::scene::isSceneEntity;
using levain::scene::restoreSnapshot;
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

bool mentions(const std::vector<std::string>& lines, std::string_view fragment)
{
    return std::ranges::any_of(lines, [&](const std::string& line)
                               { return line.find(fragment) != std::string::npos; });
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

TEST_CASE(
    "l'instantané refuse un composant qui ne se copie pas, en nommant l'entité et le composant")
{
    flecs::world world = makeWorld();
    world.entity("clé").set(Unique{});

    const auto snapshot = takeSnapshot(world);

    REQUIRE_FALSE(snapshot.has_value());
    CHECK(snapshot.error().message.find("clé") != std::string::npos);
    CHECK(snapshot.error().message.find("Unique") != std::string::npos);
    CHECK(snapshot.error().message.find("ne se copie pas") != std::string::npos);
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

TEST_CASE("l'instantané refuse une instance de prefab")
{
    flecs::world world = makeWorld();
    const flecs::entity prefab = world.prefab("modèle").set(Mass{});
    world.entity("instance").is_a(prefab);

    const auto snapshot = takeSnapshot(world);

    REQUIRE_FALSE(snapshot.has_value());
    CHECK(snapshot.error().message.find("instance") != std::string::npos);
    CHECK(snapshot.error().message.find("prefab") != std::string::npos);
}

TEST_CASE("differencesFrom est vide pour un monde que rien n'a touché")
{
    flecs::world world = makeWorld();
    const flecs::entity root = world.entity("racine").set(Transform{});
    child(world, root, "a");

    CHECK(differencesFrom(world, snapshotOf(world)).empty());
}

TEST_CASE("differencesFrom trouve chaque sorte de différence plantée")
{
    flecs::world world = makeWorld();
    const flecs::entity root = world.entity("racine").set(Mass{.kilograms = 1.0f});
    const flecs::entity a = child(world, root, "a");
    const flecs::entity b = child(world, root, "b");
    const flecs::entity other = world.entity("autre");
    const flecs::entity watcher =
        world.observer<Mass>().event(flecs::OnSet).each([](Mass&) {}).child_of(root);
    const Snapshot snapshot = snapshotOf(world);

    SUBCASE("une entité détruite")
    {
        const flecs::entity_t id = other.id();
        other.destruct();
        CHECK(mentions(differencesFrom(world, snapshot), "n'existe plus"));
        // Son index repris par une autre : la génération diffère.
        const flecs::entity taker = world.entity();
        CHECK(taker.id() != id);
        CHECK(mentions(differencesFrom(world, snapshot), "autre génération"));
    }
    SUBCASE("une entité en trop")
    {
        world.entity("née en jeu");
        CHECK(
            mentions(differencesFrom(world, snapshot), "née en jeu : entité de la scène en trop"));
    }
    SUBCASE("un nom changé")
    {
        a.set_name("renommée");
        CHECK(mentions(differencesFrom(world, snapshot), "nom « renommée » au lieu de « a »"));
    }
    SUBCASE("un nom que lookup ne retrouve plus")
    {
        // Un frère prend le nom d'un autre puis est détruit : le premier garde son nom, mais
        // l'index des noms de flecs a perdu son entrée (probe 3 de la relecture de l'ADR-0037).
        const flecs::entity extra = child(world, root, "x");
        const Snapshot withExtra = snapshotOf(world);
        extra.set_name("b");
        extra.destruct();
        const std::vector<std::string> lines = differencesFrom(world, withExtra);
        CHECK(mentions(lines, "lookup du chemin"));
        CHECK(b.name() == "b"); // le nom n'a pas bougé : seul lookup voit la différence
    }
    SUBCASE("un parent changé")
    {
        a.set(flecs::Parent{b});
        CHECK(mentions(differencesFrom(world, snapshot), "parent racine::b au lieu de racine"));
    }
    SUBCASE("un composant en trop, un manquant, une étiquette et une paire")
    {
        a.add<Tag>();
        b.add<Likes>(a);
        root.remove<Mass>();
        const std::vector<std::string> lines = differencesFrom(world, snapshot);
        CHECK(mentions(lines, "a : composant en trop"));
        CHECK(mentions(lines, "b : composant en trop"));
        CHECK(mentions(lines, "racine : composant manquant"));
    }
    SUBCASE("une valeur changée")
    {
        root.set(Mass{.kilograms = 2.0f});
        CHECK(mentions(differencesFrom(world, snapshot), "la valeur de"));
    }
    SUBCASE("l'ordre des enfants")
    {
        // flecs veut tous les enfants, l'observateur interne compris (ecs_set_child_order).
        const std::vector<flecs::entity_t> swapped{b.id(), a.id(), watcher.id()};
        ecs_set_child_order(world, root, swapped.data(), 3);
        CHECK(mentions(differencesFrom(world, snapshot), "les enfants ou leur ordre diffèrent"));
    }
    SUBCASE("un enfant interne détruit")
    {
        watcher.destruct();
        CHECK(mentions(differencesFrom(world, snapshot), "l'enfant interne"));
    }
}

TEST_CASE("differencesFrom plafonne ses lignes et compte les autres")
{
    flecs::world world = makeWorld();
    for (int i = 0; i < 30; ++i)
    {
        world.entity().set(Mass{});
    }
    const Snapshot snapshot = snapshotOf(world);
    world.each([](flecs::entity, Mass& mass) { mass.kilograms = 9.0f; });

    const std::vector<std::string> lines = differencesFrom(world, snapshot);

    REQUIRE(lines.size() == levain::scene::MaxReportedDifferences + 1);
    CHECK(lines.back() == "… et 10 autres différences");
}

TEST_CASE("createIdRanges sépare l'édition, [1, 2^24 - 1], du jeu, au-dessus")
{
    flecs::world world;
    const auto ranges = levain::scene::createIdRanges(world);
    REQUIRE(ranges.has_value());

    const flecs::entity edited = world.entity();
    levain::scene::usePlayIds(world, *ranges);
    const flecs::entity played = world.entity();
    levain::scene::useEditIds(world, *ranges);
    const flecs::entity edited2 = world.entity();

    CHECK(static_cast<std::uint32_t>(edited.id()) < levain::scene::PlayFirstIndex);
    CHECK(static_cast<std::uint32_t>(played.id()) >= levain::scene::PlayFirstIndex);
    CHECK(static_cast<std::uint32_t>(edited2.id()) < levain::scene::PlayFirstIndex);
    CHECK(levain::scene::bornInPlay(world, played));
    CHECK_FALSE(levain::scene::bornInPlay(world, edited));
}

TEST_CASE("createIdRanges refuse un monde qui a détruit une entité, en Release aussi")
{
    flecs::world world;
    world.entity().destruct();

    const auto ranges = levain::scene::createIdRanges(world);

    REQUIRE_FALSE(ranges.has_value());
    CHECK(ranges.error().message.find("détruit une entité") != std::string::npos);
    CHECK(ecs_entity_range_get(world) == nullptr); // le monde n'est pas touché
}

TEST_CASE("createIdRanges refuse de créer les plages deux fois")
{
    flecs::world world;
    REQUIRE(levain::scene::createIdRanges(world).has_value());

    CHECK_FALSE(levain::scene::createIdRanges(world).has_value());
}

TEST_CASE("l'index d'une caisse détruite en jeu revient, avec sa génération")
{
    flecs::world world = makeWorld();
    const auto ranges = levain::scene::createIdRanges(world);
    REQUIRE(ranges.has_value());
    world.entity("recyclée").destruct(); // la caisse aura la génération 1
    const flecs::entity crate = world.entity("caisse").set(Transform{});
    const flecs::entity_t crateId = crate.id();
    REQUIRE((crateId >> 32U) == 1);
    const Snapshot snapshot = snapshotOf(world);

    levain::scene::usePlayIds(world, *ranges);
    crate.destruct();
    // Ce que le jeu crée ensuite et qui reste : une requête nommée prendrait l'index libre de la
    // caisse sans plage de jeu ; dans celle du jeu, elle n'y touche pas.
    const flecs::entity query =
        world.entity(world.query_builder<Transform>("requête").build().entity());
    const flecs::entity born = world.entity("née en jeu").set(Transform{});
    const flecs::entity_t bornId = born.id();
    REQUIRE(restoreSnapshot(world, *ranges, snapshot).has_value());

    CHECK(world.is_alive(crateId));
    CHECK(ecs_get_alive(world, static_cast<std::uint32_t>(crateId)) == crateId);
    CHECK_FALSE(world.is_alive(bornId));
    CHECK(query.is_alive());
    CHECK(static_cast<std::uint32_t>(query.id()) != static_cast<std::uint32_t>(crateId));
    // Revenue dans la plage de l'édition : la liste d'index libres ne la redonne plus à personne.
    // L'ordre des étapes 2 et 3 : revenir à l'édition avant le réveil. Réveillée dans la plage du
    // jeu, la caisse resterait dans la liste d'index libres de l'édition, et la prochaine entité de
    // l'édition prendrait son index.
    CHECK(ecs_entity_range_get(world) == ranges->edit);
    const flecs::entity next = world.entity();
    CHECK(static_cast<std::uint32_t>(next.id()) < levain::scene::PlayFirstIndex);
    CHECK(static_cast<std::uint32_t>(next.id()) != static_cast<std::uint32_t>(crateId));
    CHECK(world.is_alive(crateId));
}

TEST_CASE("l'enfant de la scène d'une entité née en jeu revient avec elle détruite")
{
    flecs::world world = makeWorld();
    const auto ranges = levain::scene::createIdRanges(world);
    REQUIRE(ranges.has_value());
    const flecs::entity kid = world.entity("enfant").set(Transform{});
    const flecs::entity_t kidId = kid.id();
    const Snapshot snapshot = snapshotOf(world);

    levain::scene::usePlayIds(world, *ranges);
    const flecs::entity parent = world.entity("parent né en jeu");
    kid.set(flecs::Parent{parent}); // l'enfant part avec le parent détruit
    REQUIRE(restoreSnapshot(world, *ranges, snapshot).has_value());

    CHECK_FALSE(parent.is_alive());
    CHECK(world.is_alive(kidId));
}

TEST_CASE("restoreSnapshot refuse un index pris par une autre génération, sans rien réveiller")
{
    flecs::world world = makeWorld();
    const auto ranges = levain::scene::createIdRanges(world);
    REQUIRE(ranges.has_value());
    const flecs::entity lost = world.entity("perdue");
    const flecs::entity kept = world.entity("gardée");
    const flecs::entity_t lostId = lost.id();
    const flecs::entity_t keptId = kept.id();
    const Snapshot snapshot = snapshotOf(world);
    lost.destruct();
    kept.destruct();
    // Dans l'édition, hors du jeu : le nouveau prend l'index libéré en dernier, de génération 1.
    const flecs::entity taker = world.entity();
    REQUIRE((taker.id() >> 32U) == 1);
    levain::scene::usePlayIds(world, *ranges);
    const flecs::entity born = world.entity();

    const auto restored = restoreSnapshot(world, *ranges, snapshot);

    REQUIRE_FALSE(restored.has_value());
    CHECK(restored.error().message.find("autre génération") != std::string::npos);
    CHECK(taker.is_alive());
    CHECK(born.is_alive()); // refusé avant l'étape 1 : le monde est intact
    CHECK(ecs_entity_range_get(world) == ranges->play);
    CHECK_FALSE(world.is_alive(lostId)); // aucun réveil, même pour celle dont l'index est libre
    CHECK_FALSE(world.is_alive(keptId));
}

TEST_CASE("restoreSnapshot en mode différé est refusé, le monde intact")
{
    flecs::world world = makeWorld();
    const auto ranges = levain::scene::createIdRanges(world);
    REQUIRE(ranges.has_value());
    const Snapshot snapshot = snapshotOf(world);
    levain::scene::usePlayIds(world, *ranges);
    const flecs::entity born = world.entity("née en jeu");

    world.defer_begin();
    const auto restored = restoreSnapshot(world, *ranges, snapshot);
    world.defer_end();

    REQUIRE_FALSE(restored.has_value());
    CHECK(restored.error().message.find("mode différé") != std::string::npos);
    CHECK(born.is_alive()); // l'étape 1 n'a pas tourné
    CHECK(ecs_entity_range_get(world) == ranges->play);
}

TEST_CASE("l'instantané refuse deux frères du même nom, en nommant l'entité")
{
    flecs::world world = makeWorld();
    const flecs::entity parent = world.entity("parent");
    child(world, parent, "jumeau");
    child(world, parent, "jumeau");

    const auto snapshot = takeSnapshot(world);

    REQUIRE_FALSE(snapshot.has_value());
    CHECK(snapshot.error().message.find("jumeau") != std::string::npos);
    CHECK(snapshot.error().message.find("deux frères") != std::string::npos);
}

namespace
{

/// Un composant sans constructeur par défaut, posé par `emplace` : `ecs_value_new` ne saurait pas
/// en construire la copie.
struct NoDefault
{
    explicit NoDefault(int start) : value(start) {}

    int value;
};

} // namespace

TEST_CASE("l'instantané refuse un composant sans constructeur par défaut")
{
    flecs::world world = makeWorld();
    world.entity("sans défaut").emplace<NoDefault>(3);

    const auto snapshot = takeSnapshot(world);

    REQUIRE_FALSE(snapshot.has_value());
    CHECK(snapshot.error().message.find("sans défaut") != std::string::npos);
    CHECK(snapshot.error().message.find("NoDefault") != std::string::npos);
}

TEST_CASE(
    "l'instantané refuse une entité à Transform rangée par ChildOf sous autre chose qu'un module")
{
    flecs::world world = makeWorld();
    world.entity("rangée::dessous").set(Transform{}); // « rangée » est une entité ordinaire

    const auto snapshot = takeSnapshot(world);

    REQUIRE_FALSE(snapshot.has_value());
    CHECK(snapshot.error().message.find("dessous") != std::string::npos);
    CHECK(snapshot.error().message.find("ChildOf") != std::string::npos);
}
