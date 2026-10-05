// La marche du plugin `character` (ADR-0028) : la vitesse demandée au moteur, gravité et saut
// compris, la rotation vers la marche, ce que lit l'animation, puis le tout dans un monde flecs.

#include <cmath>

#include <doctest/doctest.h>
#include <flecs.h>
#include <glm/gtc/quaternion.hpp>

#include "levain/animation/animator.hpp"
#include "levain/character/walk.hpp"
#include "levain/physics/character.hpp"
#include "levain/physics/components.hpp"
#include "levain/physics/physics.hpp"
#include "levain/scene/components.hpp"
#include "levain/scene/fixed_step.hpp"
#include "levain/scene/scene.hpp"

using levain::character::Walker;
using levain::character::WalkInput;
using levain::physics::CharacterState;
using levain::physics::GroundState;
using levain::scene::Transform;

namespace
{

constexpr float Step = 1.0f / 60.0f;

CharacterState standing(const glm::vec3& groundVelocity = glm::vec3{0.0f})
{
    CharacterState state;
    state.ground = {.state = GroundState::OnGround,
                    .normal = {0.0f, 1.0f, 0.0f},
                    .velocity = groundVelocity,
                    .body = 1};
    return state;
}

glm::vec2 forwardOf(const glm::quat& rotation)
{
    const glm::vec3 forward = rotation * glm::vec3{0.0f, 0.0f, -1.0f};
    return {forward.x, forward.z};
}

void advance(flecs::world& world, int steps)
{
    levain::scene::FixedStep step;
    for (int i = 0; i < steps; ++i)
    {
        levain::scene::advanceWorld(world, step, Step);
    }
}

} // namespace

TEST_CASE("au sol, la gravité s'ajoute quand même, et le saut part du sol")
{
    const Walker walker;
    const glm::vec3 still = levain::character::walkVelocity(walker, {}, standing(), {}, Step);
    CHECK(still.y == doctest::Approx(-9.81f * Step)); // plaqué au sol, comme dans l'exemple de Jolt

    const glm::vec3 jump =
        levain::character::walkVelocity(walker, {.jump = true}, standing(), {}, Step);
    CHECK(jump.y == doctest::Approx(walker.jumpSpeed - 9.81f * Step));

    // Déjà en train de monter, à 4 m/s, le sol encore sous lui : pas de second saut.
    CharacterState rising = standing();
    rising.velocity = {0.0f, 4.0f, 0.0f};
    const glm::vec3 again =
        levain::character::walkVelocity(walker, {.jump = true}, rising, {0.0f, 4.0f, 0.0f}, Step);
    CHECK(again.y == doctest::Approx(4.0f - 9.81f * Step));

    // En l'air : il garde sa vitesse verticale, la gravité en plus, et ne saute pas.
    const glm::vec3 falling = levain::character::walkVelocity(
        walker, {.jump = true}, CharacterState{}, {0.0f, -2.0f, 0.0f}, Step);
    CHECK(falling.y == doctest::Approx(-2.0f - 9.81f * Step));
}

TEST_CASE("en montant, un plafond l'arrête net, et en l'air sans rien demander il garde son élan")
{
    const Walker walker;
    // Il montait à 4 m/s, le moteur l'a arrêté (vitesse effective nulle) : il ne reste pas collé.
    const glm::vec3 bumped =
        levain::character::walkVelocity(walker, {}, CharacterState{}, {0.0f, 4.0f, 0.0f}, Step);
    CHECK(bumped.y == doctest::Approx(-9.81f * Step));
    // Sans plafond, la vitesse effective suit la demandée : rien ne change.
    CharacterState rising;
    rising.velocity = {0.0f, 4.0f, 0.0f};
    CHECK(levain::character::walkVelocity(walker, {}, rising, {0.0f, 4.0f, 0.0f}, Step).y ==
          doctest::Approx(4.0f - 9.81f * Step));
    // L'élan d'une plateforme dont il a sauté : gardé, pas freiné.
    const glm::vec3 drifting =
        levain::character::walkVelocity(walker, {}, CharacterState{}, {3.0f, 1.0f, 0.0f}, Step);
    CHECK(drifting.x == doctest::Approx(3.0f));
}

TEST_CASE("à l'horizontale, il rejoint la vitesse demandée à son accélération, moins en l'air")
{
    const Walker walker;
    const WalkInput east{.direction = {1.0f, 0.0f}};
    // 20 m/s² pendant un pas : 0,33 m/s de gagné, vers 1,5 m/s.
    const glm::vec3 first = levain::character::walkVelocity(walker, east, standing(), {}, Step);
    CHECK(first.x == doctest::Approx(20.0f * Step));
    // En l'air, 30 % de l'accélération.
    const glm::vec3 air = levain::character::walkVelocity(walker, east, CharacterState{}, {}, Step);
    CHECK(air.x == doctest::Approx(0.3f * 20.0f * Step));
    // Proche du but, il l'atteint sans le dépasser.
    const glm::vec3 near =
        levain::character::walkVelocity(walker, east, standing(), {1.4f, 0.0f, 0.0f}, Step);
    CHECK(near.x == doctest::Approx(walker.walkSpeed));
    // En diagonale, une manette rend (1, 1) : ramené à 1, il ne va pas plus vite en biais.
    glm::vec3 diagonal{0.0f};
    for (int i = 0; i < 60; ++i)
    {
        diagonal = levain::character::walkVelocity(walker, {.direction = {1.0f, 1.0f}, .run = true},
                                                   standing(), diagonal, Step);
    }
    CHECK(glm::length(glm::vec2{diagonal.x, diagonal.z}) == doctest::Approx(walker.runSpeed));
}

TEST_CASE("sur une plateforme, immobile veut dire à la vitesse de la plateforme")
{
    const Walker walker;
    const glm::vec3 platform{1.0f, 0.0f, 0.0f};
    const glm::vec3 carried =
        levain::character::walkVelocity(walker, {}, standing(platform), platform, Step);
    CHECK(carried.x == doctest::Approx(1.0f));

    // Ce que lit l'animation : la vitesse par rapport au sol, nulle ici.
    CharacterState state = standing(platform);
    state.velocity = platform;
    const levain::animation::CharacterMotion motion = levain::character::motionOf(state);
    CHECK(motion.speed == doctest::Approx(0.0f));
    CHECK(motion.grounded);
}

TEST_CASE("il se tourne vers sa marche par le plus court chemin, à sa vitesse de rotation")
{
    const Walker walker;
    const glm::quat facingNorth{1.0f, 0.0f, 0.0f, 0.0f}; // l'avant vers −z
    // Vers +x : un quart de tour à droite, 90°, plus que les 12° d'un pas à 720°/s.
    const glm::quat once = levain::character::turnTowards(walker, facingNorth, {1.0f, 0.0f}, Step);
    const glm::vec2 forward = forwardOf(once);
    CHECK(std::atan2(forward.x, -forward.y) == doctest::Approx(glm::radians(12.0f)));

    glm::quat rotation = facingNorth;
    for (int i = 0; i < 10; ++i)
    {
        rotation = levain::character::turnTowards(walker, rotation, {1.0f, 0.0f}, Step);
    }
    CHECK(forwardOf(rotation).x == doctest::Approx(1.0f));
    // À l'arrêt, il garde son orientation.
    CHECK(levain::character::turnTowards(walker, rotation, {}, Step) == rotation);

    // De 170° à −170° : 20° par la gauche, pas 340° par la droite. Un pas de 12° mène à 182°, soit
    // −178°.
    const glm::quat at170 = glm::angleAxis(glm::radians(170.0f), glm::vec3{0.0f, 1.0f, 0.0f});
    const float target = glm::radians(-170.0f);
    const glm::vec2 wanted{-std::sin(target), -std::cos(target)};
    const glm::vec2 turned = forwardOf(levain::character::turnTowards(walker, at170, wanted, Step));
    CHECK(std::atan2(-turned.x, -turned.y) == doctest::Approx(glm::radians(-178.0f)));
}

TEST_CASE("dans un monde flecs, il marche, monte un escalier, saute, et l'animation le suit")
{
    flecs::world world;
    world.import<levain::character::WalkModule>();
    world.entity("Sol")
        .set(Transform{.position = {0.0f, -0.5f, 0.0f}})
        .set(levain::physics::Collider{.shape = levain::physics::Box{{50.0f, 0.5f, 50.0f}}});
    for (int step = 1; step <= 6; ++step)
    {
        // Six marches de 15 cm, profondes de 30 cm, puis un palier, comme celui de la démo.
        const float height = 0.15f * static_cast<float>(step);
        world.entity()
            .set(Transform{
                .position = {2.5f + 0.3f * static_cast<float>(step), height / 2.0f, 0.0f}})
            .set(levain::physics::Collider{.shape =
                                               levain::physics::Box{{1.5f, height / 2.0f, 2.0f}}});
    }
    const flecs::entity fox = world.entity("Renard")
                                  .set(Transform{})
                                  .set(levain::physics::CharacterController{})
                                  .set(Walker{.walkSpeed = 1.5f,
                                              .runSpeed = 4.0f,
                                              .acceleration = 20.0f,
                                              .airControl = 0.3f,
                                              .jumpSpeed = 5.0f,
                                              .gravity = 9.81f,
                                              .turnDegreesPerSecond = 720.0f})
                                  .set(WalkInput{.direction = {1.0f, 0.0f}})
                                  .set(levain::animation::CharacterMotion{});

    advance(world, 30);
    CHECK(fox.get<levain::animation::CharacterMotion>().speed ==
          doctest::Approx(1.5f).epsilon(0.02));
    CHECK(forwardOf(fox.get<Transform>().rotation).x == doctest::Approx(1.0f));

    advance(world, 180);
    CHECK(fox.get<Transform>().position.y == doctest::Approx(0.9f).epsilon(0.03)); // sur le palier

    fox.get_mut<WalkInput>() = {.jump = true};
    float highest = 0.0f;
    for (int i = 0; i < 90; ++i)
    {
        advance(world, 1);
        highest = std::max(highest, fox.get<Transform>().position.y);
        CHECK_FALSE(fox.get<WalkInput>().jump); // consommé au premier pas
    }
    // 5 m/s vers le haut sous 9,81 m/s² : 1,27 m au-dessus du palier.
    CHECK(highest - 0.9f == doctest::Approx(1.27f).epsilon(0.05));
    CHECK(fox.get<levain::animation::CharacterMotion>().grounded);
}

// Sans `StandStill` (ADR-0028), la gravité que la marche ajoute même au sol le fait descendre :
// mesuré, 21 cm en 3 s sur 30°.
TEST_CASE("à l'arrêt, il tient sur une pente de 30°, et la monte quand il marche")
{
    flecs::world world;
    world.import<levain::character::WalkModule>();
    world.entity("Sol")
        .set(Transform{.position = {0.0f, -0.5f, 0.0f}})
        .set(levain::physics::Collider{.shape = levain::physics::Box{{50.0f, 0.5f, 50.0f}}});
    const float angle = glm::radians(30.0f);
    world.entity("Rampe")
        .set(Transform{.rotation = glm::angleAxis(angle, glm::vec3{0.0f, 0.0f, 1.0f})})
        .set(levain::physics::Collider{.shape = levain::physics::Box{{4.0f, 0.1f, 2.0f}}});
    // Posé au-dessus de la rampe, en x = 1 : son dessus y est à 0,1 / cos 30° + tan 30°.
    const float surface = 0.1f / std::cos(angle) + std::tan(angle);
    const flecs::entity fox = world.entity("Renard")
                                  .set(Transform{.position = {1.0f, surface + 0.05f, 0.0f}})
                                  .set(levain::physics::CharacterController{})
                                  .set(Walker{})
                                  .set(WalkInput{});
    advance(world, 30);
    const glm::vec3 settled = fox.get<Transform>().position;
    advance(world, 180);

    CHECK(glm::length(fox.get<Transform>().position - settled) < 0.005f);
    CHECK(fox.get<CharacterState>().ground.state == GroundState::OnGround);

    fox.get_mut<WalkInput>().direction = {1.0f, 0.0f};
    advance(world, 60);
    CHECK(fox.get<Transform>().position.y > settled.y + 0.5f);
}
