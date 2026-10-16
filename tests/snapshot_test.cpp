#include <algorithm>
#include <cstdint>
#include <format>
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

namespace
{

/// Un composant que personne n'a décrit : ni réflexion ni crochet de comparaison, donc inconnu de
/// `sameValue`. Stop le réécrit toujours.
struct Opaque
{
    int count = 0;
};

std::vector<flecs::entity_t> orderedChildren(const flecs::world& world, flecs::entity_t parent)
{
    const ecs_entities_t kids = ecs_get_ordered_children(world, parent);
    return {kids.ids, kids.ids + kids.count};
}

} // namespace

TEST_CASE(
    "le critère : après Stop la scène est identique, identifiants et ordre des enfants compris")
{
    using levain::scene::Velocity;
    flecs::world world = makeWorld();
    const auto ranges = levain::scene::createIdRanges(world);
    REQUIRE(ranges.has_value());
    world.entity("jetable").destruct(); // des identifiants de génération 1
    world.entity("autre jetable").destruct();

    // La scène d'avant Play.
    const flecs::entity volume =
        world.entity("volume").set(Transform{}).set(Mass{.kilograms = 3.0f});
    const flecs::entity a = child(world, volume, "a").set(Transform{});
    const flecs::entity b = child(world, volume, "b").set(Transform{});
    child(world, volume, "c").set(Transform{});
    world.observer<Mass>("guet").event(flecs::OnSet).each([](Mass&) {}).child_of(volume);
    const flecs::entity grid = world.entity("grille").set(Transform{});
    const flecs::entity g1 = child(world, grid, "g1");
    const flecs::entity g2 = child(world, grid, "g2");
    const flecs::entity g3 = child(world, grid, "g3");
    const flecs::entity crate = world.entity("caisse").add<Tag>().set(Mass{.kilograms = 8.0f});
    const flecs::entity pile = world.entity("pile").set(Mass{}).add<Likes>(volume);
    const flecs::entity x = child(world, pile, "x");
    const flecs::entity top = world.entity("haut");
    const flecs::entity under = child(world, top, "bas");
    world.entity("opaque").set(Opaque{.count = 7});
    const Snapshot snapshot = snapshotOf(world);
    const std::vector<flecs::entity_t> idsBefore = levain::scene::sceneEntities(world);
    const std::vector<flecs::entity_t> orderBefore = orderedChildren(world, grid);

    // Le jeu : tout ce que la scène peut subir.
    levain::scene::usePlayIds(world, *ranges);
    const flecs::entity born = world.entity("née en jeu").set(Transform{});
    child(world, born, "petite");
    volume.destruct(); // un parent et ses trois enfants, avec son observateur
    g1.set_name("g2"); // deux frères s'échangent leurs noms
    g2.set_name("g1");
    g3.set_name("g1"); // un troisième prend un nom déjà pris, puis est détruit
    g3.destruct();
    x.set_name("y");
    under.remove<flecs::Parent>(); // le parent et l'enfant s'inversent
    top.set(flecs::Parent{under});
    crate.remove<Tag>().add<Likes>(grid).set(Mass{.kilograms = 1.0f}); // étiquette, paire, valeur
    pile.remove<Mass>().set(Velocity{.linear = {1.0f, 2.0f, 3.0f}});
    grid.set(Transform{.position = {5.0f, 0.0f, 0.0f}});
    world.entity("opaque").set(Opaque{.count = 9});
    const std::vector<flecs::entity_t> reversed{g2.id(), g1.id()}; // et les enfants se réordonnent
    ecs_set_child_order(world, grid, reversed.data(), 2);
    REQUIRE_FALSE(differencesFrom(world, snapshot).empty());

    const auto restored = restoreSnapshot(world, *ranges, snapshot);

    INFO((restored ? std::string{} : restored.error().message));
    REQUIRE(restored.has_value());
    const std::vector<std::string> remaining = differencesFrom(world, snapshot);
    REQUIRE(remaining.size() == 1); // l'observateur de volume ne revient pas
    CHECK(mentions(remaining, "l'enfant interne"));
    CHECK(restored->lostInternalChildren.size() == 1);
    CHECK(levain::scene::sceneEntities(world) ==
          idsBefore); // mêmes identifiants, générations comprises
    CHECK(orderedChildren(world, grid) == orderBefore);
    CHECK(world.lookup("grille::g1").id() == g1.id());
    CHECK(world.lookup("grille::g3").id() == g3.id());
    CHECK(world.lookup("volume::b").id() == b.id());
    CHECK(world.lookup("pile::x").id() == x.id());
    CHECK(a.parent() == volume);
    CHECK(under.parent() == top);
    CHECK_FALSE(top.has<flecs::Parent>());
    CHECK(crate.has<Tag>());
    CHECK_FALSE(crate.has<Likes>(grid));
    CHECK(pile.has<Likes>(volume));
    CHECK(pile.has<Mass>());
    CHECK_FALSE(pile.has<Velocity>());
    CHECK(crate.get<Mass>().kilograms == 8.0f);
    CHECK(world.lookup("opaque").get<Opaque>().count == 7);
}

TEST_CASE("Stop retire de la scène un composant Transient posé en jeu")
{
    flecs::world world = makeWorld();
    const auto ranges = levain::scene::createIdRanges(world);
    REQUIRE(ranges.has_value());
    world.component<Unique>().add<levain::scene::Transient>();
    const flecs::entity body = world.entity("corps").set(Transform{});
    const Snapshot snapshot = snapshotOf(world);

    levain::scene::usePlayIds(world, *ranges);
    body.set(Unique{std::make_unique<int>(4)}); // un handle de Jolt, par exemple
    REQUIRE(restoreSnapshot(world, *ranges, snapshot).has_value());

    CHECK_FALSE(body.has<Unique>());
}

TEST_CASE("Stop rend un volume sans son observateur détruit, et le rapport le nomme")
{
    flecs::world world = makeWorld();
    const auto ranges = levain::scene::createIdRanges(world);
    REQUIRE(ranges.has_value());
    const flecs::entity volume = world.entity("zone").set(Transform{});
    world.observer<Mass>("alarme").event(flecs::OnSet).each([](Mass&) {}).child_of(volume);
    const Snapshot snapshot = snapshotOf(world);

    levain::scene::usePlayIds(world, *ranges);
    volume.destruct();
    const auto restored = restoreSnapshot(world, *ranges, snapshot);

    REQUIRE(restored.has_value());
    REQUIRE(restored->lostInternalChildren.size() == 1);
    CHECK(restored->lostInternalChildren[0].find("zone") != std::string::npos);
    CHECK(volume.is_alive());
}

TEST_CASE("compareWithSnapshot compte les vraies différences à part des enfants internes perdus")
{
    flecs::world world = makeWorld();
    const auto ranges = levain::scene::createIdRanges(world);
    REQUIRE(ranges.has_value());
    // Plus de vingt volumes à observateur (le plafond des lignes est 20), et une entité à part.
    std::vector<flecs::entity> volumes;
    for (std::size_t i = 0; i < levain::scene::MaxReportedDifferences + 5; ++i)
    {
        const flecs::entity volume =
            world.entity(std::format("zone_{}", i).c_str()).set(Transform{});
        world.observer<Mass>().event(flecs::OnSet).each([](Mass&) {}).child_of(volume);
        volumes.push_back(volume);
    }
    const flecs::entity alone = world.entity("seule").set(Mass{.kilograms = 1.0f});
    const Snapshot snapshot = snapshotOf(world);

    levain::scene::usePlayIds(world, *ranges);
    for (const flecs::entity& volume : volumes)
    {
        volume.destruct();
    }
    REQUIRE(restoreSnapshot(world, *ranges, snapshot).has_value());
    alone.set(Mass{.kilograms = 2.0f}); // la seule vraie différence

    const levain::scene::Comparison comparison =
        levain::scene::compareWithSnapshot(world, snapshot);

    CHECK(comparison.differenceCount == 1);
    CHECK(comparison.differences.size() == 1);
    CHECK(comparison.lostInternalChildren.size() == volumes.size());
    // `differencesFrom` garde son contrat : tout, plafonné.
    CHECK(differencesFrom(world, snapshot).size() == levain::scene::MaxReportedDifferences + 1);
}

TEST_CASE("restoreSnapshot refuse une liste d'enfants qui n'est pas celle du parent")
{
    flecs::world world = makeWorld();
    const auto ranges = levain::scene::createIdRanges(world);
    REQUIRE(ranges.has_value());
    const flecs::entity parent = world.entity("parent");
    child(world, parent, "a");
    const flecs::entity stranger = world.entity("étranger");
    Snapshot snapshot = snapshotOf(world);
    auto record =
        std::ranges::find(snapshot.entities, parent.id(), &levain::scene::EntityRecord::id);
    REQUIRE(record != snapshot.entities.end());
    record->children.push_back(stranger.id()); // flecs, en Release, copierait au-delà de la liste

    const auto restored = restoreSnapshot(world, *ranges, snapshot);

    REQUIRE_FALSE(restored.has_value());
    CHECK(restored.error().message.find("parent") != std::string::npos);
    CHECK(restored.error().message.find("ne sont pas ceux de l'instantané") != std::string::npos);
}

TEST_CASE(
    "Stop vérifie son résultat : une entité créée hors de la plage du jeu reste et est nommée")
{
    flecs::world world = makeWorld();
    const auto ranges = levain::scene::createIdRanges(world);
    REQUIRE(ranges.has_value());
    const Snapshot snapshot = snapshotOf(world);

    // Le jeu oublie `usePlayIds` : l'entité prend un index de l'édition, que Stop ne sait pas née
    // en jeu (le test d'index) ; la vérification finale la nomme au lieu de la laisser passer.
    world.entity("oubliée en jeu");
    const auto restored = restoreSnapshot(world, *ranges, snapshot);

    REQUIRE_FALSE(restored.has_value());
    CHECK(restored.error().message.find("oubliée en jeu : entité de la scène en trop") !=
          std::string::npos);
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

TEST_CASE("l'instantané accepte une entité à Transform rangée sous un descendant d'un module")
{
    flecs::world world = makeWorld();
    world.entity("atelier").add(flecs::Module);
    // « rig » n'est pas un module, mais il est rangé dans la portée d'« atelier » : le contenu d'un
    // module, à n'importe quelle profondeur, n'est pas la scène.
    world.entity("atelier::rig::camera").set(Transform{});

    CHECK(takeSnapshot(world).has_value());
}

TEST_CASE("Stop nomme une entité née en jeu sous une entité de la scène par ChildOf")
{
    flecs::world world = makeWorld();
    const auto ranges = levain::scene::createIdRanges(world);
    REQUIRE(ranges.has_value());
    const flecs::entity owner = world.entity("maître").set(Transform{});
    const Snapshot snapshot = snapshotOf(world);

    levain::scene::usePlayIds(world, *ranges);
    world.entity("jouet").child_of(owner).set(Transform{}); // née en jeu, que Stop ne détruit pas
    const auto restored = restoreSnapshot(world, *ranges, snapshot);

    REQUIRE_FALSE(restored.has_value());
    CHECK(restored.error().message.find("jouet") != std::string::npos);
    CHECK(restored.error().message.find("par ChildOf") != std::string::npos);
}

TEST_CASE("Stop rend l'ordre de beaucoup d'enfants renversés")
{
    flecs::world world = makeWorld();
    const auto ranges = levain::scene::createIdRanges(world);
    REQUIRE(ranges.has_value());
    const flecs::entity parent = world.entity("père");
    for (int i = 0; i < 20000; ++i)
    {
        world.entity().set(flecs::Parent{parent});
    }
    const Snapshot snapshot = snapshotOf(world);
    const std::vector<flecs::entity_t> before = orderedChildren(world, parent);
    std::vector<flecs::entity_t> reversed(before.rbegin(), before.rend());
    ecs_set_child_order(world, parent, reversed.data(), static_cast<std::int32_t>(reversed.size()));

    REQUIRE(restoreSnapshot(world, *ranges, snapshot).has_value());

    CHECK(orderedChildren(world, parent) == before);
}
