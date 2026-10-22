#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <doctest/doctest.h>
#include <flecs.h>

#include "levain/scene/snapshot.hpp"
#include "levain/scene/tombstone.hpp"

// La pierre tombale (ADR-0038, décision 3, morceau A1) : supprimer réserve l'index, annuler rend
// l'identifiant d'avant, et un index pris est un refus nommé dans tous les builds. Les
// comportements de flecs dont tout cela dépend sont épinglés à part
// (`flecs_tombstone_pins_test.cpp`).

using levain::scene::checkTombstonesHold;
using levain::scene::removeKeepingIndex;
using levain::scene::reviveFromTombstones;
using levain::scene::sceneEntities;
using levain::scene::Tombstone;
using levain::scene::Tombstones;

namespace
{

struct Marker
{
    float value = 0.0F;
};

std::uint32_t indexOf(flecs::entity_t id)
{
    return static_cast<std::uint32_t>(id);
}

/// Le monde de Levain : les deux plages d'identifiants, comme `createApp` les crée. `Tombstone`
/// n'est volontairement pas enregistré ici : la première suppression doit s'en charger seule.
flecs::world makeWorld()
{
    flecs::world world;
    REQUIRE(levain::scene::createIdRanges(world).has_value());
    world.component<Marker>();
    return world;
}

flecs::entity child(const flecs::world& world, flecs::entity parent, const char* name)
{
    return world.entity().set(flecs::Parent{parent}).set_name(name);
}

/// Ce qu'un éditeur crée sans le savoir entre une suppression et son annulation : des entités, une
/// requête nommée, un observateur. Rend leurs index.
std::vector<std::uint32_t> createBystanders(flecs::world& world)
{
    std::vector<std::uint32_t> indexes;
    indexes.reserve(1002);
    for (int i = 0; i < 1000; ++i)
    {
        indexes.push_back(indexOf(world.entity().set(Marker{}).id()));
    }
    static int counter = 0;
    const auto query =
        world.query_builder<Marker>(("requête " + std::to_string(++counter)).c_str()).build();
    indexes.push_back(indexOf(query.entity().id()));
    indexes.push_back(
        indexOf(world.observer<Marker>().event(flecs::OnAdd).each([](Marker&) {}).id()));
    return indexes;
}

bool contains(const std::vector<std::uint32_t>& indexes, std::uint32_t index)
{
    return std::ranges::find(indexes, index) != indexes.end();
}

/// Une suppression dont la pierre du parent est détruite à la main : la prochaine entité prend son
/// index. Rend le parent, l'enfant (qui garde sa pierre), la suppression et le nouveau venu.
struct TakenIndex
{
    flecs::entity root;
    flecs::entity kid;
    Tombstones removed;
    flecs::entity newcomer;
};

TakenIndex takeAParentIndex(flecs::world& world)
{
    const flecs::entity root = world.entity("Cube");
    const flecs::entity kid = child(world, root, "enfant");
    auto removed = removeKeepingIndex(world, root.id());
    REQUIRE(removed.has_value());
    REQUIRE(removed->reserved.size() == 2);
    ecs_delete(world, removed->reserved[0].stone);
    const flecs::entity newcomer = world.entity("nouveau");
    REQUIRE(indexOf(newcomer.id()) == indexOf(root.id()));
    return {root, kid, std::move(*removed), newcomer};
}

} // namespace

TEST_CASE("supprimer puis annuler rend le même identifiant, malgré mille créations, une requête "
          "et un observateur")
{
    flecs::world world = makeWorld();
    const flecs::entity_t cube = world.entity("Cube").set(Marker{2.0F}).id();

    auto removed = removeKeepingIndex(world, cube);
    REQUIRE(removed.has_value());
    CHECK_FALSE(ecs_is_alive(world, cube));
    REQUIRE(removed->reserved.size() == 1);
    CHECK(removed->reserved[0].entity == cube);
    CHECK(indexOf(removed->reserved[0].stone) == indexOf(cube));
    CHECK(ecs_has_id(world, removed->reserved[0].stone, world.id<Tombstone>()));

    // Le contre-test de ce contrôle est « sans pierre » : ces mille entités reprendraient l'index.
    CHECK_FALSE(contains(createBystanders(world), indexOf(cube)));

    REQUIRE(reviveFromTombstones(world, *removed).has_value());
    CHECK(ecs_is_alive(world, cube));
    CHECK(ecs_get_alive(world, indexOf(cube)) == cube); // identifiant et génération d'avant
    CHECK_FALSE(world.entity(cube).has<Marker>());      // vide : rendre le reste est à l'appelant
    CHECK_FALSE(ecs_is_alive(world, removed->reserved[0].stone)); // la pierre est partie
}

template <int N> struct Filler
{
    int value = N;
};

/// Épuise les 256 identifiants bas que flecs réserve aux composants : au-delà, un composant
/// enregistré prend un identifiant ordinaire, donc un index libéré.
template <int... Ns> void registerFillers(flecs::world& world, std::integer_sequence<int, Ns...>)
{
    (world.component<Filler<Ns>>(), ...);
}

TEST_CASE("la première suppression enregistre Tombstone avant de détruire, même passé les "
          "identifiants bas de flecs")
{
    // Un monde où personne n'a encore employé `Tombstone`, et dont les 256 identifiants bas des
    // composants sont pris. Un `Tombstone` enregistré APRÈS la destruction prendrait l'index libéré
    // et deviendrait sa propre pierre (essai du relecteur, contre-test de ce test).
    flecs::world world = makeWorld();
    registerFillers(world, std::make_integer_sequence<int, 300>{});
    const flecs::entity_t cube = world.entity("Cube").id();
    auto removed = removeKeepingIndex(world, cube);
    REQUIRE(removed.has_value());

    CHECK(removed->reserved[0].stone != world.id<Tombstone>());
    CHECK(indexOf(world.entity().id()) != indexOf(cube));
    CHECK(ecs_get_alive(world, indexOf(cube)) == removed->reserved[0].stone);
}

TEST_CASE("supprimer un parent réserve tout son sous-arbre, parent d'abord, et l'annulation le "
          "réveille en entier")
{
    flecs::world world = makeWorld();
    const flecs::entity root = world.entity("Volume").set(Marker{});
    const flecs::entity a = child(world, root, "a");
    const flecs::entity deep = child(world, a, "profond");
    const flecs::entity b = child(world, root, "b");
    // L'observateur d'un `onEnter` : rangé sous le volume par ChildOf, il part avec lui.
    const flecs::entity internal =
        world.observer<Marker>().event(flecs::OnSet).each([](Marker&) {}).child_of(root);
    const std::vector<flecs::entity_t> all{root.id(), a.id(), deep.id(), b.id()};
    const std::vector<flecs::entity_t> before = sceneEntities(world);
    REQUIRE(before.size() == all.size());

    auto removed = removeKeepingIndex(world, root.id());
    REQUIRE(removed.has_value());
    REQUIRE(removed->reserved.size() == all.size());
    // L'enfant `ChildOf` part avec le volume, rapporté par son nom, jamais réservé.
    REQUIRE(removed->lost.size() == 1);
    CHECK(removed->lost[0].id == internal.id());
    CHECK_FALSE(removed->lost[0].name.empty());
    CHECK(ecs_get_alive(world, indexOf(internal.id())) != removed->lost[0].id);
    CHECK(removed->reserved.front().entity == root.id());
    for (const auto& reserved : removed->reserved)
    {
        CHECK_FALSE(ecs_is_alive(world, reserved.entity));
        CHECK(ecs_get_alive(world, indexOf(reserved.entity)) == reserved.stone);
    }
    // Un enfant vient toujours après son parent : l'annulation peut rattacher dans cet ordre.
    const auto position = [&](flecs::entity_t id)
    {
        for (std::size_t i = 0; i < removed->reserved.size(); ++i)
        {
            if (removed->reserved[i].entity == id)
            {
                return i;
            }
        }
        return removed->reserved.size();
    };
    CHECK(position(root.id()) < position(a.id()));
    CHECK(position(a.id()) < position(deep.id()));
    CHECK(position(root.id()) < position(b.id()));

    createBystanders(world);
    REQUIRE(reviveFromTombstones(world, *removed).has_value());
    for (const flecs::entity_t id : all)
    {
        CHECK(ecs_get_alive(world, indexOf(id)) == id);
    }
    // L'enfant interne ne revient pas, même vide : la scène n'a pas une entité fantôme de plus.
    CHECK_FALSE(ecs_is_alive(world, internal.id()));
    CHECK(sceneEntities(world).size() == before.size() + 1000); // les mille créées, rien de plus
}

TEST_CASE("refaire repose les mêmes pierres, et la génération ne monte pas en trois tours")
{
    flecs::world world = makeWorld();
    const flecs::entity_t created = world.entity("neuve").id();

    for (int round = 0; round < 3; ++round)
    {
        auto removed = removeKeepingIndex(world, created); // annuler la création
        REQUIRE(removed.has_value());
        CHECK(removed->reserved[0].stone == ECS_GENERATION_INC(created));
        createBystanders(world);
        REQUIRE(reviveFromTombstones(world, *removed).has_value()); // refaire
        CHECK(ecs_get_alive(world, indexOf(created)) == created);
    }
}

TEST_CASE("un index pris est nommé par la garde, dans tous les builds")
{
    flecs::world world = makeWorld();
    const TakenIndex taken = takeAParentIndex(world);

    const auto held = checkTombstonesHold(world, taken.removed);
    REQUIRE_FALSE(held.has_value());
    CHECK(held.error().message.find(std::to_string(indexOf(taken.root.id()))) != std::string::npos);
    CHECK(held.error().message.find("pierre tombale") != std::string::npos);
}

TEST_CASE("annuler sur un index pris refuse, le nouveau vivant et le monde intact")
{
    flecs::world world = makeWorld();
    const TakenIndex taken = takeAParentIndex(world);

    const auto revived = reviveFromTombstones(world, taken.removed);
    // Le nouveau, qu'un `ecs_make_alive` écraserait en Release sans rien dire, est vivant et nommé.
    CHECK(ecs_is_alive(world, taken.newcomer.id()));
    CHECK(world.lookup("nouveau").id() == taken.newcomer.id());
    // Et rien n'a été fait à la pierre de l'enfant, qui tient toujours son index.
    CHECK(ecs_get_alive(world, indexOf(taken.kid.id())) == taken.removed.reserved[1].stone);
    REQUIRE_FALSE(revived.has_value());
    CHECK(revived.error().message.find("le monde n'est pas touché") != std::string::npos);
}

TEST_CASE("une pierre qui n'est plus une pierre est un refus aussi")
{
    flecs::world world = makeWorld();
    const flecs::entity_t cube = world.entity("Cube").id();
    auto removed = removeKeepingIndex(world, cube);
    REQUIRE(removed.has_value());

    // Même index, même génération, mais l'étiquette a été retirée : ce n'est plus la pierre.
    ecs_remove_id(world, removed->reserved[0].stone, world.id<Tombstone>());
    CHECK_FALSE(checkTombstonesHold(world, *removed).has_value());
    CHECK_FALSE(reviveFromTombstones(world, *removed).has_value());
}

TEST_CASE("supprimer refuse ce qui n'est pas une entité vivante de la scène, le monde intact")
{
    flecs::world world = makeWorld();
    const flecs::entity_t cube = world.entity("Cube").id();

    SUBCASE("une entité morte")
    {
        const flecs::entity_t dead = world.entity().id();
        world.entity(dead).destruct();
        const auto removed = removeKeepingIndex(world, dead);
        REQUIRE_FALSE(removed.has_value());
        CHECK(removed.error().message.find("pas vivante") != std::string::npos);
    }
    SUBCASE("ce qui n'est pas de la scène")
    {
        const flecs::entity_t component = world.id<Marker>();
        const auto removed = removeKeepingIndex(world, component);
        REQUIRE_FALSE(removed.has_value());
        CHECK(removed.error().message.find("pas une entité de la scène") != std::string::npos);
        CHECK(ecs_is_alive(world, component));
    }
    SUBCASE("une pierre")
    {
        auto removed = removeKeepingIndex(world, cube);
        REQUIRE(removed.has_value());
        const auto again = removeKeepingIndex(world, removed->reserved[0].stone);
        REQUIRE_FALSE(again.has_value());
        CHECK(again.error().message.find("pierre tombale") != std::string::npos);
        CHECK(ecs_is_alive(world, removed->reserved[0].stone));
    }
}

TEST_CASE("un observateur qui crée une entité pendant la destruction ne fait pas d'une entité "
          "une pierre")
{
    // Un `onExit` de gameplay (physics.cpp) : `OnRemove` crée une entité qui prend l'index libéré
    // d'un enfant, à la génération de sa pierre. `ecs_make_alive` n'y changerait rien, et la pierre
    // serait l'entité de quelqu'un d'autre.
    flecs::world world = makeWorld();
    std::vector<flecs::entity_t> born;
    world.observer<Marker>()
        .event(flecs::OnRemove)
        .each(
            [&](flecs::entity entity, Marker&)
            {
                if (born.empty())
                {
                    born.push_back(entity.world().entity("née pendant la suppression").id());
                }
            });
    const flecs::entity root = world.entity("Volume").set(Marker{});
    child(world, root, "enfant");

    const auto removed = removeKeepingIndex(world, root.id());
    REQUIRE_FALSE(born.empty());
    REQUIRE_FALSE(removed.has_value());
    CHECK(removed.error().message.find("repris pendant la destruction") != std::string::npos);
    for (const flecs::entity_t id : born)
    {
        CHECK_FALSE(
            ecs_has_id(world, id, world.id<Tombstone>())); // l'entité d'un autre n'est pas marquée
        CHECK(ecs_is_alive(world, id));
    }
}
