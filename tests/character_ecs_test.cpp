// Le personnage par les entités flecs (ADR-0028) : un `CharacterController` en fait un personnage,
// le gameplay écrit sa `CharacterVelocity` et sa rotation, le module recopie sa position et pose
// son `CharacterState`. La logique elle-même est dans `character_test.cpp`.

#include <cmath>

#include <doctest/doctest.h>
#include <flecs.h>
#include <glm/gtc/quaternion.hpp>

#include "levain/physics/character.hpp"
#include "levain/physics/components.hpp"
#include "levain/physics/physics.hpp"
#include "levain/physics/physics_world.hpp"
#include "levain/scene/components.hpp"
#include "levain/scene/fixed_step.hpp"
#include "levain/scene/scene.hpp"

using levain::physics::Box;
using levain::physics::CharacterController;
using levain::physics::CharacterHandle;
using levain::physics::CharacterState;
using levain::physics::CharacterVelocity;
using levain::physics::Collider;
using levain::physics::GroundState;
using levain::physics::Layer;
using levain::physics::PhysicsWorld;
using levain::scene::Transform;

namespace
{

constexpr float Step = 1.0f / 60.0f;

void advance(flecs::world& world, int steps)
{
    levain::scene::FixedStep step;
    for (int i = 0; i < steps; ++i)
    {
        levain::scene::advanceWorld(world, step, Step);
    }
}

/// Un monde avec un sol dont le dessus est à y = 0, et la gravité que le gameplay donne à ses
/// personnages : le moteur ne la leur applique pas (ADR-0028). Ce qu'écrira le plugin de marche,
/// réduit à la chute.
flecs::world characterWorld()
{
    flecs::world world;
    world.import<levain::physics::PhysicsModule>();
    world.entity("Sol")
        .set(Transform{.position = {0.0f, -0.5f, 0.0f}})
        .set(Collider{.shape = Box{{50.0f, 0.5f, 50.0f}}});
    world.system<CharacterVelocity, const CharacterState>("Fall")
        .kind<levain::scene::Simulation>()
        .each(
            [](flecs::iter& it, std::size_t, CharacterVelocity& velocity,
               const CharacterState& state)
            {
                velocity.value.y = levain::physics::isWalking(state.ground)
                                       ? 0.0f
                                       : velocity.value.y - 9.81f * it.delta_time();
            });
    return world;
}

flecs::entity spawnPlayer(flecs::world& world, const glm::vec3& feet)
{
    return world.entity("Joueur").set(Transform{.position = feet}).set(CharacterController{});
}

const PhysicsWorld& physicsOf(const flecs::world& world)
{
    return world.get<PhysicsWorld>();
}

} // namespace

TEST_CASE("un CharacterController fait un personnage : il tombe, se pose, et son Transform suit")
{
    flecs::world world = characterWorld();
    const flecs::entity player = spawnPlayer(world, {0.0f, 1.0f, 0.0f});
    advance(world, 60);

    REQUIRE(player.has<CharacterHandle>());
    CHECK(player.has<levain::scene::PreviousTransform>()); // interpolé par le rendu, comme un corps
    CHECK(levain::physics::characterCount(physicsOf(world)) == 1);
    CHECK(player.get<Transform>().position.y == doctest::Approx(0.01f).epsilon(0.01));
    CHECK(player.get<CharacterState>().ground.state == GroundState::OnGround);
    CHECK(player.get<CharacterState>().ground.body == world.lookup("Sol").id());
}

TEST_CASE("sa vitesse voulue persiste, et sa vitesse effective tombe à zéro contre un mur")
{
    flecs::world world = characterWorld();
    world.entity("Mur")
        .set(Transform{.position = {2.0f, 1.0f, 0.0f}})
        .set(Collider{.shape = Box{{0.5f, 1.0f, 2.0f}}});
    const flecs::entity player = spawnPlayer(world, {0.0f, 0.0f, 0.0f});
    advance(world, 1);
    player.get_mut<CharacterVelocity>().value.x = 2.0f; // posée une fois, jamais reposée
    advance(world, 15);

    CHECK(player.get<CharacterState>().velocity.x == doctest::Approx(2.0f).epsilon(0.01));
    advance(world, 60);
    // Arrêté contre le mur (en x = 1,5), son rayon et sa marge avant.
    CHECK(player.get<Transform>().position.x == doctest::Approx(1.5f - 0.3f - 0.02f).epsilon(0.02));
    CHECK(player.get<CharacterVelocity>().value.x == 2.0f);
    CHECK(std::abs(player.get<CharacterState>().velocity.x) < 0.01f);
}

TEST_CASE(
    "le gameplay le tourne par référence, autour de Y seulement, et un Transform posé le téléporte")
{
    flecs::world world = characterWorld();
    const flecs::entity player = spawnPlayer(world, {0.0f, 0.0f, 0.0f});
    advance(world, 1);
    const glm::quat yaw = glm::angleAxis(glm::radians(90.0f), glm::vec3{0.0f, 1.0f, 0.0f});
    player.get_mut<Transform>().rotation = yaw;
    player.get_mut<CharacterVelocity>().value.x = 1.0f;
    advance(world, 1);

    const CharacterHandle handle = player.get<CharacterHandle>();
    const glm::quat turned = levain::physics::characterPose(physicsOf(world), handle).rotation;
    CHECK((turned * glm::vec3{0.0f, 0.0f, -1.0f}).x == doctest::Approx(-1.0f));
    CHECK(player.get<Transform>().position.x > 0.0f); // il a avancé, sans être téléporté

    player.set(Transform{.position = {10.0f, 5.0f, 0.0f}});
    // Hors du pipeline, l'observateur part tout de suite : l'état est déjà celui de la nouvelle
    // place.
    CHECK(player.get<CharacterState>().ground.state == GroundState::InAir);
    advance(world, 1);
    CHECK(player.get<CharacterVelocity>().value.x == 0.0f); // remise à zéro avec la téléportation
    CHECK(player.get<Transform>().position.x == doctest::Approx(10.0f));
    advance(world, 1);
    CHECK(player.get<Transform>().position.y < 5.0f); // et il tombe déjà, depuis sa nouvelle place
    CHECK(player.get<Transform>().position.y > 4.9f);
}

// Le cas qui justifie le point de synchronisation du pas (README, invariant 6) : un `set` fait par
// le gameplay pendant Simulation n'est appliqué qu'à la fusion, qui doit venir avant
// MoveCharacters.
TEST_CASE("un Transform posé par un système de gameplay le téléporte avant qu'il ne bouge")
{
    flecs::world world = characterWorld();
    const flecs::entity player = spawnPlayer(world, {0.0f, 0.0f, 0.0f});
    advance(world, 1);
    player.get_mut<CharacterVelocity>().value.x = 1.0f;
    bool teleported = false;
    world.system("Teleport")
        .kind<levain::scene::Simulation>()
        .run(
            [&teleported, player](flecs::iter&)
            {
                if (!teleported)
                {
                    player.set(Transform{.position = {10.0f, 0.0f, 0.0f}});
                    teleported = true;
                }
            });
    advance(world, 1);

    CHECK(player.get<Transform>().position.x == doctest::Approx(10.0f));
    CHECK(player.get<CharacterVelocity>().value.x == 0.0f);
    CHECK(player.get<levain::scene::PreviousTransform>().transform.position.x ==
          doctest::Approx(10.0f));
}

TEST_CASE("un volume déclencheur voit l'entité du personnage entrer, par son corps intérieur")
{
    flecs::world world = characterWorld();
    const flecs::entity water =
        world.entity("Eau")
            .set(Transform{.position = {0.0f, 0.5f, 0.0f}})
            .set(Collider{.shape = Box{{2.0f, 0.5f, 2.0f}}, .layer = Layer::Sensor});
    const flecs::entity player = spawnPlayer(world, {0.0f, 0.0f, 0.0f});
    advance(world, 2);

    CHECK(player.has<levain::physics::InsideOf>(water));
}

TEST_CASE("retirer le CharacterController ou détruire l'entité détruit le personnage")
{
    flecs::world world = characterWorld();
    const flecs::entity first = spawnPlayer(world, {0.0f, 0.0f, 0.0f});
    advance(world, 1);
    REQUIRE(levain::physics::characterCount(physicsOf(world)) == 1);
    const std::uint32_t bodiesWithCharacter = levain::physics::bodyCount(physicsOf(world));

    first.remove<CharacterController>();
    CHECK_FALSE(first.has<CharacterHandle>());
    CHECK_FALSE(first.has<CharacterState>()); // plus de personnage, plus d'état qu'on lirait périmé
    CHECK(levain::physics::characterCount(physicsOf(world)) == 0);
    CHECK(levain::physics::bodyCount(physicsOf(world)) == bodiesWithCharacter - 1);

    const flecs::entity second = world.entity().set(Transform{}).set(CharacterController{});
    advance(world, 1);
    REQUIRE(levain::physics::characterCount(physicsOf(world)) == 1);
    second.destruct();
    CHECK(levain::physics::characterCount(physicsOf(world)) == 0);
}

// Un refus est une donnée, pas un bug (ADR-0034) : il se teste dans tous les presets.
TEST_CASE("un personnage avec un Collider, ou enfant d'une autre entité, est refusé")
{
    flecs::world world = characterWorld();
    const flecs::entity withCollider =
        spawnPlayer(world, {0.0f, 0.0f, 0.0f}).set(Collider{.shape = Box{}});
    // Par flecs::Parent, la hiérarchie du moteur (ADR-0015) : c'est elle que la règle regarde.
    const flecs::entity child = world.entity(flecs::Parent{world.entity().set(Transform{})})
                                    .set(Transform{})
                                    .set(CharacterController{});
    advance(world, 1);

    CHECK_FALSE(withCollider.has<CharacterHandle>());
    CHECK_FALSE(child.has<CharacterHandle>());
    CHECK(levain::physics::characterCount(physicsOf(world)) == 0);
}
