#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

#include <doctest/doctest.h>
#include <flecs.h>

#include "levain/assets/asset_ref.hpp"
#include "levain/scene/components.hpp"
#include "levain/scene/reflection.hpp"
#include "levain/scene/scene.hpp"

using levain::scene::rangeOf;
using levain::scene::sameValue;

namespace reflection_test
{

enum class Gait : std::uint8_t
{
    Walk,
    Run,
};

struct Capsule
{
    float halfHeight = 0.5f;
    float radius = 0.25f;
};

/// Des champs de types variés, du remplissage entre eux, et un agrégat imbriqué.
struct Probe
{
    bool flag = false;
    glm::vec3 position{0.0f};
    Capsule shape{};
    float speed = 0.0f;
};

/// Un composant à la `Transform` : des feuilles glm, une enum, un booléen, un type imbriqué, une
/// référence d'entité, et du remplissage entre eux.
struct Mover
{
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    Gait gait = Gait::Walk;
    bool grounded = true;
    Capsule shape{};
    float speed = 2.0f;
    flecs::entity target{};
};

struct Padded
{
    bool flag = false;
    float value = 0.0f;
};

struct Marker // une étiquette, sans champ, entre aussi par sa déclaration
{
};

struct Weights // un tableau C : décrit à la main, en tableau en ligne
{
    float w[4] = {};
};

/// Des déclarations dans la portée d'un module : le chemin de leurs types n'est pas leur symbole.
/// `Capsule`, décrit par récursion dans `Mover`, se déclare après lui.
struct ReflectionTestModule
{
    explicit ReflectionTestModule(flecs::world& world)
    {
        world.module<ReflectionTestModule>();
        world.import<levain::scene::SceneModule>(); // les feuilles glm
        levain::scene::describeAuthored<Mover>(world).range(&Mover::speed, 0.0, 10.0);
        levain::scene::describe<Capsule>(world).range(&Capsule::radius, 0.01, 10.0);
        levain::scene::describe<Padded>(world);
        levain::scene::describeAuthored<Marker>(world);
    }
};

} // namespace reflection_test

using reflection_test::Capsule;
using reflection_test::Gait;
using reflection_test::Marker;
using reflection_test::Mover;
using reflection_test::Padded;
using reflection_test::Probe;
using reflection_test::Weights;

namespace
{

const ecs_member_t& memberOf(flecs::world& world, flecs::entity_t type, const char* name)
{
    const ecs_member_t* member = ecs_struct_get_member(world, type, name);
    REQUIRE(member != nullptr); // lève : pas de déréférencement nul
    return *member;
}

bool sameRange(const ecs_member_t& member, double min, double max)
{
    const auto range = rangeOf(member).value_or(ecs_member_value_range_t{});
    return range.min == min && range.max == max;
}

} // namespace

TEST_CASE("les champs d'un agrégat se lisent dans la struct : nombre, noms, décalages")
{
    using levain::scene::detail::FieldCount;
    using levain::scene::detail::fieldName;
    using levain::scene::detail::isIdentifier;
    using levain::scene::detail::offsetInProbe;
    using levain::scene::detail::tieFields;

    static_assert(FieldCount<Probe> == 4); // l'agrégat imbriqué compte pour un champ
    static_assert(FieldCount<Capsule> == 2);
    // Le garde-fou de fieldName : un nom mal découpé (« _M_elems[2] » de libstdc++) n'est pas un
    // identifiant.
    static_assert(isIdentifier("speed") && isIdentifier("_m_value2"));
    static_assert(!isIdentifier("") && !isIdentifier("2fast") && !isIdentifier("_M_elems[2]"));
    CHECK(fieldName<Probe, 0>() == "flag");
    CHECK(fieldName<Probe, 1>() == "position");
    CHECK(fieldName<Probe, 2>() == "shape");
    CHECK(fieldName<Probe, 3>() == "speed");
    const Probe probe{};
    CHECK(offsetInProbe(probe, std::get<0>(tieFields(probe))) == offsetof(Probe, flag));
    CHECK(offsetInProbe(probe, std::get<1>(tieFields(probe))) == offsetof(Probe, position));
    CHECK(offsetInProbe(probe, std::get<2>(tieFields(probe))) == offsetof(Probe, shape));
    CHECK(offsetInProbe(probe, std::get<3>(tieFields(probe))) == offsetof(Probe, speed));
}

TEST_CASE("une déclaration lit les champs dans la struct : noms, décalages, types")
{
    flecs::world world;
    world.import<reflection_test::ReflectionTestModule>();

    struct Expected
    {
        std::string_view name;
        std::size_t offset;
        flecs::entity_t type;
    };

    const Expected expected[] = {
        {"position", offsetof(Mover, position), world.id<glm::vec3>()},
        {"rotation", offsetof(Mover, rotation), world.id<glm::quat>()},
        {"gait", offsetof(Mover, gait), world.id<Gait>()},
        {"grounded", offsetof(Mover, grounded), world.id<bool>()},
        {"shape", offsetof(Mover, shape), world.id<Capsule>()},
        {"speed", offsetof(Mover, speed), world.id<float>()},
        {"target", offsetof(Mover, target), world.id<flecs::entity>()},
    };
    const EcsStruct& description = world.entity(world.id<Mover>()).get<flecs::Struct>();
    REQUIRE(std::cmp_equal(description.members.count, std::size(expected)));
    const auto* members = static_cast<const ecs_member_t*>(description.members.array);
    for (std::size_t i = 0; i < std::size(expected); ++i)
    {
        CAPTURE(expected[i].name);
        CHECK(std::string_view{members[i].name} == expected[i].name);
        CHECK(std::cmp_equal(members[i].offset, expected[i].offset));
        CHECK(members[i].type == expected[i].type);
        CHECK(members[i].count == 0); // un scalaire, pas un tableau d'un élément
    }
    CHECK(world.entity(world.id<Gait>()).has<flecs::Enum>());
}

TEST_CASE("une déclaration pose Authored et ses bornes, un type imbriqué déclaré après aussi")
{
    flecs::world world;
    world.import<reflection_test::ReflectionTestModule>();

    CHECK(world.entity(world.id<Mover>()).has<levain::scene::Authored>());
    CHECK_FALSE(world.entity(world.id<Capsule>()).has<levain::scene::Authored>());
    CHECK(world.entity(world.id<Marker>()).has<levain::scene::Authored>());
    CHECK(sameRange(memberOf(world, world.id<Mover>(), "speed"), 0.0, 10.0));
    CHECK(sameRange(memberOf(world, world.id<Capsule>(), "radius"), 0.01, 10.0));
    CHECK_FALSE(rangeOf(memberOf(world, world.id<Mover>(), "gait")).has_value());
    CHECK_FALSE(rangeOf(memberOf(world, world.id<Capsule>(), "halfHeight")).has_value());
}

TEST_CASE("décrire deux fois garde les membres et les bornes, un type imbriqué déclaré avant aussi")
{
    // Les mêmes déclarations deux fois, comme deux modules qui appellent la même fonction.
    flecs::world world;
    world.import<levain::scene::SceneModule>();
    for (int declaration = 0; declaration < 2; ++declaration)
    {
        levain::scene::describe<Capsule>(world).range(&Capsule::radius, 0.01, 10.0);
        levain::scene::describeAuthored<Mover>(world).range(&Mover::speed, 0.0, 10.0);
    }

    CHECK(world.entity(world.id<Capsule>()).get<flecs::Struct>().members.count == 2);
    CHECK(world.entity(world.id<Mover>()).get<flecs::Struct>().members.count == 7);
    CHECK(sameRange(memberOf(world, world.id<Capsule>(), "radius"), 0.01, 10.0));
    CHECK(sameRange(memberOf(world, world.id<Mover>(), "speed"), 0.0, 10.0));
    CHECK_FALSE(world.entity(world.id<Capsule>()).has<levain::scene::Authored>());
}

TEST_CASE("le JSON d'une entité écrit chaque champ, l'enum par son nom et l'entité par son chemin")
{
    flecs::world world;
    world.import<reflection_test::ReflectionTestModule>();
    const flecs::entity player = world.entity("player");
    const flecs::entity hero = world.entity("hero").set(Mover{.position = {1.0f, 2.0f, 3.0f},
                                                              .gait = Gait::Run,
                                                              .grounded = false,
                                                              .speed = 4.5f,
                                                              .target = player});

    const flecs::string json = hero.to_json();
    CHECK(std::string_view{json.c_str()}.find(
              R"("reflection_test.ReflectionTestModule.Mover":{"position":{"x":1, "y":2, "z":3}, )"
              R"("rotation":{"x":0, "y":0, "z":0, "w":1}, "gait":"Run", "grounded":false, )"
              R"("shape":{"halfHeight":0.5, "radius":0.25}, "speed":4.5, "target":"player"})") !=
          std::string_view::npos);
}

TEST_CASE("setComponentValue déclenche OnSet une fois, sur un composant à hook on_replace aussi")
{
    flecs::world world;
    world.import<reflection_test::ReflectionTestModule>();
    world.import<levain::assets::AssetsModule>();
    int moverSets = 0;
    int meshSets = 0;
    world.observer<const Mover>()
        .event(flecs::OnSet)
        .each([&moverSets](const Mover&) { ++moverSets; });
    world.observer<const levain::assets::MeshRef>()
        .event(flecs::OnSet)
        .each([&meshSets](const levain::assets::MeshRef&) { ++meshSets; });
    const levain::assets::AssetId before{.high = 1, .low = 1};
    const levain::assets::AssetId after{.high = 2, .low = 2};
    const flecs::entity hero =
        world.entity("hero").set(Mover{}).set(levain::assets::MeshRef{.mesh = {.asset = before}});
    moverSets = 0;
    meshSets = 0;

    // L'inspecteur édite une copie, jamais la table, puis l'écrit d'un bloc.
    Mover mover = hero.get<Mover>();
    mover.speed = 7.0f;
    levain::scene::setComponentValue(world, hero, world.id<Mover>(), &mover);
    const levain::assets::MeshRef mesh{.mesh = {.asset = after}};
    levain::scene::setComponentValue(world, hero, world.id<levain::assets::MeshRef>(), &mesh);

    CHECK(moverSets == 1);
    CHECK(hero.get<Mover>().speed == doctest::Approx(7.0f));
    CHECK(meshSets == 1);
    CHECK(levain::assets::referenceCount(world, before) == 0); // le hook a vu les deux valeurs
    CHECK(levain::assets::referenceCount(world, after) == 1);
}

TEST_CASE("sameValue compare feuille par feuille, sans voir le remplissage")
{
    flecs::world world;
    world.import<reflection_test::ReflectionTestModule>();

    // Deux Padded égaux dont les trois octets de remplissage diffèrent : memcmp les dit différents.
    static_assert(offsetof(Padded, value) > sizeof(bool));
    alignas(Padded) std::byte zeros[sizeof(Padded)]{};
    alignas(Padded) std::byte ones[sizeof(Padded)]{};
    std::memset(ones, 0xff, sizeof ones);
    const Padded value{.flag = true, .value = 1.5f};
    for (std::byte* bytes : {zeros, ones})
    {
        std::memcpy(bytes + offsetof(Padded, flag), &value.flag, sizeof value.flag);
        std::memcpy(bytes + offsetof(Padded, value), &value.value, sizeof value.value);
    }
    REQUIRE(std::memcmp(zeros, ones, sizeof zeros) != 0);
    CHECK(sameValue(world, world.id<Padded>(), zeros, ones));
    const Padded other{.flag = true, .value = 2.0f};
    CHECK_FALSE(sameValue(world, world.id<Padded>(), zeros, &other));

    // Un type imbriqué, une entité, une feuille glm : chaque feuille compte.
    const Mover mover{.target = world.entity("player")};
    Mover moved = mover;
    CHECK(sameValue(world, world.id<Mover>(), &mover, &moved));
    moved.shape.radius = 0.5f;
    CHECK_FALSE(sameValue(world, world.id<Mover>(), &mover, &moved));
    moved = mover;
    moved.target = world.entity("other");
    CHECK_FALSE(sameValue(world, world.id<Mover>(), &mover, &moved));
    moved = mover;
    moved.rotation.w = 0.5f;
    CHECK_FALSE(sameValue(world, world.id<Mover>(), &mover, &moved));

    // Un tableau flecs (la glm::mat4 de WorldTransform), un champ tableau en ligne, décrit à la
    // main, et une étiquette sans valeur.
    const levain::scene::WorldTransform matrix{};
    levain::scene::WorldTransform shifted{};
    CHECK(sameValue(world, world.id<levain::scene::WorldTransform>(), &matrix, &shifted));
    shifted.matrix[3][2] = 1.0f;
    CHECK_FALSE(sameValue(world, world.id<levain::scene::WorldTransform>(), &matrix, &shifted));
    world.component<Weights>().member<float>("w", 4, offsetof(Weights, w));
    const Weights weights{};
    Weights heavier{};
    CHECK(sameValue(world, world.id<Weights>(), &weights, &heavier));
    heavier.w[3] = 1.0f;
    CHECK_FALSE(sameValue(world, world.id<Weights>(), &weights, &heavier));
    const Marker marker{};
    CHECK(sameValue(world, world.id<Marker>(), &marker, &marker));
}

TEST_CASE("la clé d'un composant est son symbole, relu comme symbole, pas son chemin")
{
    flecs::world world;
    world.import<reflection_test::ReflectionTestModule>();
    const flecs::entity_t mover = world.id<Mover>();

    // Enregistré dans la portée du module, son chemin dépend de qui l'a enregistré en premier.
    const std::string path = world.entity(mover).path(".", "").c_str();
    CHECK(path == "reflection_test.ReflectionTestModule.Mover");
    CHECK(levain::scene::stableKeyOf(world, mover) == "reflection_test.Mover");
    CHECK(levain::scene::componentOfKey(world, levain::scene::stableKeyOf(world, mover)) == mover);
    CHECK(levain::scene::componentOfKey(world, path) == 0);
    const flecs::entity_t transform = world.id<levain::scene::Transform>();
    CHECK(levain::scene::componentOfKey(world, "levain.scene.Transform") == transform);
}
