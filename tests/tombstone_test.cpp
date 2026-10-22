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
// l'identifiant d'avant. Les comportements de flecs dont tout cela dépend sont épinglés à part
// (`flecs_tombstone_pins_test.cpp`).

using levain::scene::removeKeepingIndex;
using levain::scene::reviveFromTombstones;
using levain::scene::Tombstone;

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
