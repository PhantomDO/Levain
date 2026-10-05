// Le personnage de M6.3 (ADR-0028), au niveau du monde physique : il tombe, monte des marches et
// des pentes, et s'arrête contre un rebord. Ce qu'il pousse, ce qui le porte et ce qui le voit ont
// leurs tests plus bas ; la marche du plugin `character` et la glu flecs, les leurs.

#include <cmath>
#include <cstdint>
#include <optional>

#include <doctest/doctest.h>
#include <glm/gtc/quaternion.hpp>

#include "levain/physics/character.hpp"
#include "levain/physics/components.hpp"
#include "levain/physics/physics_world.hpp"
#include "levain/physics/queries.hpp"

using levain::physics::BodyPose;
using levain::physics::Box;
using levain::physics::CharacterController;
using levain::physics::CharacterHandle;
using levain::physics::Collider;
using levain::physics::GroundState;
using levain::physics::Layer;
using levain::physics::PhysicsWorld;
using levain::physics::RigidBody;

namespace
{

constexpr float Step = 1.0f / 60.0f;
constexpr float Gravity = 9.81f;
constexpr std::uint64_t GroundEntity = 1;
constexpr std::uint64_t PlayerEntity = 2;

/// Un sol de 100 m × 100 m dont le dessus est à y = 0, et rien d'autre.
PhysicsWorld worldWithGround()
{
    PhysicsWorld world = levain::physics::createPhysicsWorld();
    levain::physics::createBody(world, {.shape = Box{{50.0f, 0.5f, 50.0f}}}, nullptr,
                                {.position = {0.0f, -0.5f, 0.0f}}, GroundEntity);
    return world;
}

/// Une boîte de décor, par son coin bas à `x` et ses dimensions : ce qu'on empile pour un escalier.
void addBlock(PhysicsWorld& world, float x, float width, float height, std::uint64_t entity = 3)
{
    levain::physics::createBody(world, {.shape = Box{{width / 2.0f, height / 2.0f, 2.0f}}}, nullptr,
                                {.position = {x + width / 2.0f, height / 2.0f, 0.0f}}, entity);
}

/// Une rampe qui monte vers +x avec une pente de `degrees`, dont le dessus part du sol en `x`.
void addRamp(PhysicsWorld& world, float x, float degrees)
{
    constexpr float HalfLength = 3.0f;
    constexpr float HalfThickness = 0.1f;
    const float angle = glm::radians(degrees);
    const glm::quat rotation = glm::angleAxis(angle, glm::vec3{0.0f, 0.0f, 1.0f});
    // Le coin haut de l'extrémité basse, (−demi-longueur, +demi-épaisseur) dans le repère de la
    // rampe, doit tomber sur le sol en x : le centre s'en déduit.
    const glm::vec3 lowerTopCorner = rotation * glm::vec3{-HalfLength, HalfThickness, 0.0f};
    levain::physics::createBody(
        world, {.shape = Box{{HalfLength, HalfThickness, 2.0f}}}, nullptr,
        {.position = glm::vec3{x, 0.0f, 0.0f} - lowerTopCorner, .rotation = rotation}, 4);
}

/// Ce que fera la marche du plugin, réduit à l'essentiel : la vitesse horizontale voulue, et la
/// gravité tant qu'il ne marche pas. Au sol, la vitesse verticale repart de celle du sol.
struct Walker
{
    CharacterHandle handle;
    float verticalSpeed = 0.0f;
};

void walk(PhysicsWorld& world, Walker& walker, const glm::vec3& horizontal, float seconds)
{
    const int steps = static_cast<int>(std::lround(seconds / Step));
    for (int i = 0; i < steps; ++i)
    {
        const auto ground = levain::physics::characterGround(world, walker.handle);
        walker.verticalSpeed = levain::physics::isWalking(ground)
                                   ? ground.velocity.y
                                   : walker.verticalSpeed - Gravity * Step;
        const glm::vec3 carried = levain::physics::isWalking(ground)
                                      ? glm::vec3{ground.velocity.x, 0.0f, ground.velocity.z}
                                      : glm::vec3{0.0f};
        levain::physics::moveCharacter(
            world, walker.handle,
            horizontal + carried + glm::vec3{0.0f, walker.verticalSpeed, 0.0f}, Step);
        levain::physics::stepPhysics(world, Step);
    }
}

Walker spawn(PhysicsWorld& world, const glm::vec3& feet, const CharacterController& controller = {})
{
    return {.handle = levain::physics::createCharacter(world, controller, {.position = feet},
                                                       PlayerEntity)};
}

glm::vec3 feetOf(const PhysicsWorld& world, const Walker& walker)
{
    return levain::physics::characterPose(world, walker.handle).position;
}

/// Au sol, les pieds restent à la marge du personnage au-dessus de la surface : 2 cm, le
/// `mCharacterPadding` de Jolt, qu'il garde pour que ses déplacements touchent le moins possible.
constexpr float Padding = 0.02f;

bool standsAt(float feet, float surface)
{
    return feet >= surface - 0.001f && feet <= surface + Padding + 0.01f;
}

} // namespace

TEST_CASE("un personnage lâché en l'air tombe et se pose sur le sol, qui le porte")
{
    PhysicsWorld world = worldWithGround();
    Walker player = spawn(world, {0.0f, 1.0f, 0.0f});
    REQUIRE(player.handle.value != CharacterHandle::None);
    CHECK(levain::physics::characterCount(world) == 1);

    walk(world, player, {}, 1.0f);

    CHECK(standsAt(feetOf(world, player).y, 0.0f));
    const auto ground = levain::physics::characterGround(world, player.handle);
    CHECK(ground.state == GroundState::OnGround);
    CHECK(ground.body == GroundEntity);
    CHECK(ground.normal.y == doctest::Approx(1.0f));
}

TEST_CASE("il monte un escalier de marches de 15 cm, mais pas un rebord de 90 cm")
{
    SUBCASE("six marches de 15 cm, comme l'escalier de la démo")
    {
        PhysicsWorld world = worldWithGround();
        for (int step = 1; step <= 6; ++step)
        {
            // Chaque marche est un bloc plein jusqu'au sol, 15 cm plus haut que le précédent et
            // décalé de 30 cm : chacun dépasse du suivant d'une profondeur de marche.
            addBlock(world, 1.0f + 0.3f * static_cast<float>(step - 1), 3.0f,
                     0.15f * static_cast<float>(step));
        }
        Walker player = spawn(world, {0.0f, 0.0f, 0.0f});
        walk(world, player, {2.0f, 0.0f, 0.0f}, 2.5f); // 5 m : de x = 0 au palier, en x ≈ 5

        CHECK(feetOf(world, player).x > 4.0f);
        CHECK(standsAt(feetOf(world, player).y, 0.9f));
        CHECK(levain::physics::isWalking(levain::physics::characterGround(world, player.handle)));
    }
    SUBCASE("sans hauteur de marche, la première l'arrête : c'est bien la montée qui opère")
    {
        PhysicsWorld world = worldWithGround();
        addBlock(world, 1.0f, 3.0f, 0.15f);
        Walker player = spawn(world, {0.0f, 0.0f, 0.0f}, {.stepHeight = 0.0f});
        walk(world, player, {2.0f, 0.0f, 0.0f}, 2.0f);

        CHECK(feetOf(world, player).x < 1.0f);
        CHECK(standsAt(feetOf(world, player).y, 0.0f));
    }
    SUBCASE("un rebord de 90 cm, celui de la cour de Sponza, l'arrête")
    {
        PhysicsWorld world = worldWithGround();
        addBlock(world, 1.0f, 3.0f, 0.9f);
        Walker player = spawn(world, {0.0f, 0.0f, 0.0f});
        walk(world, player, {2.0f, 0.0f, 0.0f}, 2.0f);

        // Arrêté contre le mur : son rayon et sa marge avant x = 1, toujours au sol.
        CHECK(feetOf(world, player).x == doctest::Approx(1.0f - 0.3f - Padding).epsilon(0.02));
        CHECK(standsAt(feetOf(world, player).y, 0.0f));
    }
}

TEST_CASE("il monte une pente de 30°, et n'en gravit pas une de 55°")
{
    SUBCASE("30° : il monte, et marche dessus")
    {
        PhysicsWorld world = worldWithGround();
        addRamp(world, 1.0f, 30.0f);
        Walker player = spawn(world, {0.0f, 0.0f, 0.0f});
        walk(world, player, {2.0f, 0.0f, 0.0f}, 2.0f);

        CHECK(feetOf(world, player).y > 1.0f);
        CHECK(levain::physics::isWalking(levain::physics::characterGround(world, player.handle)));
    }
    SUBCASE("55° : au-delà de sa pente maximale de 50°, il reste en bas")
    {
        PhysicsWorld world = worldWithGround();
        addRamp(world, 1.0f, 55.0f);
        Walker player = spawn(world, {0.0f, 0.0f, 0.0f});
        walk(world, player, {2.0f, 0.0f, 0.0f}, 2.0f);

        // Arrivé contre la rampe (elle commence en x = 1, son rayon l'en écarte), sans la gravir :
        // une marche de 30 cm au plus, ce que la montée des marches lui permet contre une paroi.
        CHECK(feetOf(world, player).x > 0.6f);
        CHECK(feetOf(world, player).y < 0.3f + Padding);
    }
}

TEST_CASE("des réglages de personnage impossibles sont refusés, avec leur raison")
{
    using levain::physics::whyNotThisCharacter;
    CHECK_FALSE(whyNotThisCharacter({}).has_value());
    CHECK(whyNotThisCharacter({.shape = {.halfHeight = 0.0f, .radius = 0.3f}}).has_value());
    CHECK(whyNotThisCharacter({.maxSlopeDegrees = 90.0f}).has_value());
    CHECK(whyNotThisCharacter({.stepHeight = -0.1f}).has_value());
    CHECK(whyNotThisCharacter({.stickToFloorDistance = NAN}).has_value());
    CHECK(whyNotThisCharacter({.mass = 0.0f}).has_value());
    CHECK(whyNotThisCharacter({.maxPushForce = -1.0f}).has_value());
}

#ifdef NDEBUG
// En Debug, l'assertion arrête le programme avant : c'est voulu (règle n°7).
TEST_CASE("createCharacter refuse une masse nulle : aucun personnage, et le monde reste intact")
{
    PhysicsWorld world = worldWithGround();
    const auto refused = levain::physics::createCharacter(world, {.mass = 0.0f}, {}, PlayerEntity);
    CHECK(refused.value == CharacterHandle::None);
    CHECK(levain::physics::characterCount(world) == 0);
    CHECK(levain::physics::bodyCount(world) == 1);
}
#endif
