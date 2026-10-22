#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <doctest/doctest.h>
#include <flecs.h>

#include "levain/scene/snapshot.hpp"

// Les comportements de flecs 4.1.6 sur lesquels repose la pierre tombale (ADR-0038, décision 3,
// essais a à f). Ils n'appellent AUCUN code de Levain en dehors de `createIdRanges` : si un seul
// devient faux, c'est flecs qui a changé, et la pierre tombale est à repenser avant d'aller plus
// loin. Ils tournent en Debug et en Release (la bibliothèque flecs de vcpkg suit le preset) ; le
// seul qui dépend du build est dit en toutes lettres (« Release seulement »).

namespace
{

struct Likes
{
};

struct Stone
{
};

struct Marker
{
    float value = 0.0F;
};

/// Un champ d'entité d'un composant d'auteur : il garde l'identifiant d'une entité détruite.
struct Reference
{
    flecs::entity_t target = 0;
};

std::uint32_t indexOf(flecs::entity_t id)
{
    return static_cast<std::uint32_t>(id);
}

/// Le monde de Levain : les deux plages d'identifiants, comme `createApp` les crée.
struct World
{
    flecs::world world;
    levain::scene::IdRanges ranges;

    World()
    {
        auto created = levain::scene::createIdRanges(world);
        REQUIRE(created.has_value());
        ranges = *created;
        world.component<Marker>();
        world.component<Reference>();
        world.component<Stone>();
        world.component<Likes>();
    }
};

/// La pierre d'une entité détruite : le même index, la génération suivante. C'est ce que flecs
/// met dans l'index à la destruction (`ECS_GENERATION_INC`, src/entity.c), vérifié ici.
flecs::entity_t reserve(flecs::world& world, flecs::entity_t destroyed)
{
    REQUIRE_FALSE(ecs_is_alive(world, destroyed));
    const flecs::entity_t stone = ECS_GENERATION_INC(destroyed);
    ecs_make_alive(world, stone);
    ecs_add_id(world, stone, world.id<Stone>());
    return stone;
}

/// Mille entités, une requête, un observateur : tout ce qu'un éditeur crée sans le savoir.
/// Rend leurs index.
std::vector<std::uint32_t> createBystanders(flecs::world& world)
{
    std::vector<std::uint32_t> indexes;
    indexes.reserve(1002);
    for (int i = 0; i < 1000; ++i)
    {
        indexes.push_back(indexOf(world.entity().set(Marker{}).id()));
    }
    // Une requête sans nom n'a pas d'entité (index 0) : on la nomme, comme le fait l'éditeur.
    static int counter = 0;
    const auto query =
        world.query_builder<Marker>(("requête " + std::to_string(++counter)).c_str()).build();
    REQUIRE(query.entity().id() != 0);
    indexes.push_back(indexOf(query.entity().id()));
    const auto observer = world.observer<Marker>().event(flecs::OnAdd).each([](Marker&) {});
    indexes.push_back(indexOf(observer.id()));
    return indexes;
}

bool contains(const std::vector<std::uint32_t>& indexes, std::uint32_t index)
{
    return std::ranges::find(indexes, index) != indexes.end();
}

} // namespace

TEST_CASE("pin a : sans pierre, flecs redonne l'index détruit à la création suivante")
{
    World w;
    const flecs::entity_t victim = w.world.entity("victime").id();
    w.world.entity(victim).destruct();

    SUBCASE("une entité")
    {
        const flecs::entity_t next = w.world.entity().id();
        CHECK(indexOf(next) == indexOf(victim));
        CHECK(next != victim); // une autre génération : l'ancien identifiant reste mort
        CHECK_FALSE(ecs_is_alive(w.world, victim));
    }
    SUBCASE("une requête")
    {
        const auto query = w.world.query_builder<Marker>("requête").build();
        CHECK(indexOf(query.entity().id()) == indexOf(victim));
    }
    SUBCASE("un observateur")
    {
        const auto observer = w.world.observer<Marker>().event(flecs::OnAdd).each([](Marker&) {});
        CHECK(indexOf(observer.id()) == indexOf(victim));
    }
    SUBCASE("mille entités, une requête et un observateur")
    {
        CHECK(contains(createBystanders(w.world), indexOf(victim)));
    }
}

TEST_CASE("pin b : avec la pierre, rien ne reprend l'index")
{
    World w;
    const flecs::entity_t victim = w.world.entity("victime").id();
    w.world.entity(victim).destruct();
    const flecs::entity_t stone = reserve(w.world, victim);

    CHECK(ecs_get_alive(w.world, indexOf(victim)) == stone);
    CHECK(ecs_is_alive(w.world, stone));
    CHECK_FALSE(ecs_is_alive(w.world, victim)); // une référence à l'ancienne génération reste morte

    CHECK_FALSE(contains(createBystanders(w.world), indexOf(victim)));
    CHECK(ecs_get_alive(w.world, indexOf(victim)) == stone); // toujours elle, après tout cela
}

TEST_CASE("pin c : détruire la pierre puis réveiller la génération d'avant rend l'entité")
{
    World w;
    const flecs::entity cube = w.world.entity("Cube").set(Marker{1.0F});
    const flecs::entity_t cubeId = cube.id();
    const flecs::entity_t holder = w.world.entity("fan").set(Reference{cubeId}).id();

    ecs_delete(w.world, cubeId);
    CHECK(w.world.lookup("Cube").id() == 0);
    const flecs::entity_t stone = reserve(w.world, cubeId);
    createBystanders(w.world);

    ecs_delete(w.world, stone);
    ecs_make_alive(w.world, cubeId);

    CHECK(ecs_is_alive(w.world, cubeId));
    CHECK(ecs_get_alive(w.world, indexOf(cubeId)) ==
          cubeId); // la génération d'avant, pas la suivante
    ecs_set_name(w.world, cubeId, "Cube");
    CHECK(w.world.lookup("Cube").id() == cubeId);
    // La référence d'un composant d'auteur gardait l'identifiant mort : elle est de nouveau valide.
    const Reference& reference = w.world.entity(holder).get<Reference>();
    CHECK(reference.target == cubeId);
    CHECK(ecs_is_alive(w.world, reference.target));
}

TEST_CASE("pin d : un sous-arbre revient avec ses chemins, une création et une destruction "
          "intercalées")
{
    World w;
    const flecs::entity root = w.world.entity("Cube");
    const auto kid = [&](flecs::entity parent, const char* name)
    { return w.world.entity().set(flecs::Parent{parent}).set_name(name); };
    const flecs::entity a = kid(root, "a");
    const flecs::entity b = kid(root, "b");
    const flecs::entity c = kid(root, "Cube 2");
    const flecs::entity deep = kid(a, "profond");
    const std::vector<flecs::entity_t> order{b.id(), c.id(), a.id()};
    ecs_set_child_order(w.world, root.id(), order.data(), static_cast<std::int32_t>(order.size()));

    // « Supprimer » : tout le sous-arbre part (flecs::Parent), une pierre par index.
    const std::vector<flecs::entity_t> dying{root.id(), a.id(), b.id(), c.id(), deep.id()};
    ecs_delete(w.world, root.id());
    for (const flecs::entity_t id : dying)
    {
        CHECK_FALSE(ecs_is_alive(w.world, id)); // les enfants sont partis avec le parent
    }
    std::vector<flecs::entity_t> stones;
    stones.reserve(dying.size());
    for (const flecs::entity_t id : dying)
    {
        stones.push_back(reserve(w.world, id));
    }

    // Intercalé : une création, une destruction, une création qui reste.
    const flecs::entity_t made = w.world.entity("pendant").id();
    ecs_delete(w.world, made);
    const flecs::entity_t kept = w.world.entity("reste").id();
    const std::vector<std::uint32_t> bystanders = createBystanders(w.world);
    for (const flecs::entity_t id : dying)
    {
        CHECK_FALSE(contains(bystanders, indexOf(id)));
        CHECK(indexOf(kept) != indexOf(id));
    }

    // « Annuler » : les pierres partent, les identifiants d'avant reviennent, puis les parents.
    for (const flecs::entity_t stone : stones)
    {
        ecs_delete(w.world, stone);
    }
    for (const flecs::entity_t id : dying)
    {
        ecs_make_alive(w.world, id);
        CHECK(ecs_get_alive(w.world, indexOf(id)) == id);
    }
    const auto reparent = [&](flecs::entity_t id, flecs::entity_t parent, const char* name)
    { w.world.entity(id).set(flecs::Parent{parent}).set_name(name); };
    ecs_set_name(w.world, root.id(), "Cube");
    reparent(a.id(), root.id(), "a");
    reparent(b.id(), root.id(), "b");
    reparent(c.id(), root.id(), "Cube 2");
    reparent(deep.id(), a.id(), "profond");
    ecs_set_child_order(w.world, root.id(), order.data(), static_cast<std::int32_t>(order.size()));

    CHECK(w.world.lookup("Cube::a::profond").id() == deep.id());
    CHECK(w.world.lookup("Cube::Cube 2").id() == c.id());
    const ecs_entities_t children = ecs_get_ordered_children(w.world, root.id());
    REQUIRE(children.count == 3);
    CHECK(children.ids[0] == b.id());
    CHECK(children.ids[1] == c.id());
    CHECK(children.ids[2] == a.id());
    CHECK(ecs_is_alive(w.world, kept)); // ce qui était né entre-temps n'a pas été touché
}

TEST_CASE("pin e : une création annulée puis refaite retrouve son identifiant, sans que la "
          "génération monte")
{
    World w;
    const flecs::entity_t created = w.world.entity("neuve").id();

    for (int round = 0; round < 3; ++round)
    {
        ecs_delete(w.world, created); // annuler la création
        const flecs::entity_t stone = reserve(w.world, created);
        createBystanders(w.world); // d'autres naissent entre-temps
        ecs_delete(w.world, stone);
        ecs_make_alive(w.world, created); // refaire
        // Ce que flecs rend, lu dans l'index : l'identifiant d'origine, génération comprise.
        const flecs::entity_t alive = ecs_get_alive(w.world, indexOf(created));
        CHECK(alive == created);
        CHECK((alive >> 32U) == 0U); // la génération d'origine, après chaque tour
    }
    ecs_set_name(w.world, created, "neuve");
    CHECK(w.world.lookup("neuve").id() == created);
}

TEST_CASE("pin f : un nom à espace et à chiffre se retrouve par lookup, à la racine et sous un "
          "parent")
{
    World w;
    const flecs::entity root = w.world.entity("Cube 2");
    const flecs::entity parent = w.world.entity("Cube");
    const flecs::entity kid = w.world.entity().set(flecs::Parent{parent}).set_name("Cube 2");

    CHECK(w.world.lookup("Cube 2").id() == root.id());
    CHECK(w.world.lookup("Cube::Cube 2").id() == kid.id());
    CHECK(ecs_lookup_path_w_sep(w.world, 0, "Cube.Cube 2", ".", nullptr, true) == kid.id());
}

TEST_CASE("pin : ce que le réveil ne rend pas")
{
    World w;
    const flecs::entity cube = w.world.entity("Cube").set(Marker{3.0F});
    const flecs::entity_t cubeId = cube.id();
    const flecs::entity fan = w.world.entity("fan").add<Likes>(cube);
    // L'observateur d'un `onEnter` : rangé sous le volume par ChildOf (physics::onEnter).
    const flecs::entity internal =
        w.world.observer<Marker>().event(flecs::OnSet).each([](Marker&) {});
    internal.child_of(cube);
    const flecs::entity_t internalId = internal.id();
    REQUIRE(fan.has<Likes>(cube));

    ecs_delete(w.world, cubeId);
    const flecs::entity_t stone = reserve(w.world, cubeId);
    ecs_delete(w.world, stone);
    ecs_make_alive(w.world, cubeId);

    CHECK(ecs_is_alive(w.world, cubeId));
    CHECK_FALSE(fan.has<Likes>(cube)); // la paire (R, E) d'une autre entité est retirée pour de bon
    CHECK_FALSE(ecs_is_alive(w.world, internalId)); // l'enfant ChildOf est parti et ne revient pas
    CHECK_FALSE(
        w.world.entity(cubeId).has<Marker>()); // l'entité revient vide : types et valeurs à rendre
    CHECK(w.world.lookup("Cube").id() == 0);   // ... et sans nom
    CHECK_FALSE(w.world.entity(cubeId).has<flecs::Parent>());
}

// Release seulement : en Debug, flecs s'arrête sur `ecs_check` (src/entity.c:3133-3139) avant
// d'écraser quoi que ce soit, et le processus meurt : rien à vérifier. C'est ce silence qui rend la
// garde de Levain nécessaire dans tous les builds. La porte est le build de la **bibliothèque**
// flecs (`ecs_get_build_info()->debug`), et un CHECK exige qu'il suive celui du test (NDEBUG) :
// si vcpkg liait un jour la mauvaise, le test échouerait bruyamment au lieu de se taire.
TEST_CASE("pin : en Release, ecs_make_alive sur un index pris l'écrase sans rien dire")
{
#ifdef NDEBUG
    constexpr bool TestIsRelease = true;
#else
    constexpr bool TestIsRelease = false;
#endif
    const bool flecsIsDebug = ecs_get_build_info()->debug;
    CHECK(flecsIsDebug == !TestIsRelease);
    if (flecsIsDebug)
    {
        return; // Debug : le processus mourrait, voir plus haut
    }

    World w;
    const flecs::entity_t old = w.world.entity("ancien").id();
    w.world.entity(old).destruct();
    const flecs::entity newcomer = w.world.entity("nouveau");
    REQUIRE(indexOf(newcomer.id()) == indexOf(old));

    ecs_make_alive(w.world, old);

    CHECK(ecs_get_alive(w.world, indexOf(old)) == old); // l'ancien a pris la place
    CHECK_FALSE(ecs_is_alive(w.world, newcomer.id()));  // et le nouveau est mort, sans erreur
}
