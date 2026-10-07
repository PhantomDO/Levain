// Le personnage de M6.3 (ADR-0028), au niveau du monde physique : il tombe, monte des marches et
// des pentes, s'arrête contre un rebord, pousse des caisses et suit une plateforme. La marche du
// plugin `character` et la glu flecs ont leurs propres tests.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

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
        levain::physics::refreshCharacterGround(world, walker.handle);
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
        // Sa vitesse effective est nulle : celle que l'animation lira, pour ne pas courir sur
        // place.
        CHECK(glm::length(levain::physics::characterVelocity(world, player.handle)) < 0.01f);
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

// La force de poussée est un seuil (ADR-0028) : une caisse ne bouge que si elle dépasse son
// frottement, m < F / (μ·g). Avec 100 N et un frottement de 0,5, la limite est vers 20 kg sans
// hauteur de marche. Avec, contre une caisse plus haute que la marche, `ExtendedUpdate` le déplace
// deux fois par pas, et la caisse reçoit deux poussées : vers 40 kg. Mesuré : 35 kg avance de 1,47
// m, 40 kg de 4 cm.
TEST_CASE("sa poussée a un seuil, vers 20 kg sans hauteur de marche et le double avec")
{
    const auto pushed = [](float mass, float stepHeight)
    {
        PhysicsWorld world = worldWithGround();
        const RigidBody body{.mass = mass};
        const auto crate = levain::physics::createBody(world, {.shape = Box{{0.5f, 0.5f, 0.5f}}},
                                                       &body, {.position = {1.5f, 0.5f, 0.0f}}, 5);
        Walker player = spawn(world, {0.0f, 0.0f, 0.0f}, {.stepHeight = stepHeight});
        walk(world, player, {2.0f, 0.0f, 0.0f}, 2.0f);
        return levain::physics::bodyPose(world, crate).position.x - 1.5f;
    };
    CHECK(pushed(19.0f, 0.0f) > 1.0f);
    CHECK(pushed(21.0f, 0.0f) < 0.05f);
    CHECK(pushed(35.0f, 0.3f) > 1.0f);
    CHECK(pushed(45.0f, 0.3f) < 0.05f);
}

// Le corps intérieur fait 90 % de la capsule (ADR-0028) : une caisse s'y pose, 8 cm à l'intérieur
// de la capsule. Le personnage l'en fait sortir s'il peut la soulever, m·g < `maxPushForce` (100 N)
// : mesuré, 10 kg finit sur sa tête à 1,83 m, 20 kg reste sur le corps intérieur à 1,74 m. Dans les
// deux cas, elle se stabilise, et il ne bouge pas.
TEST_CASE(
    "une caisse lâchée sur lui se stabilise, sur sa tête s'il peut la soulever, sans le déplacer")
{
    const auto dropped = [](float mass)
    {
        PhysicsWorld world = worldWithGround();
        Walker player = spawn(world, {0.0f, 0.0f, 0.0f});
        const RigidBody body{.mass = mass};
        const auto crate = levain::physics::createBody(world, {.shape = Box{{0.2f, 0.2f, 0.2f}}},
                                                       &body, {.position = {0.0f, 3.0f, 0.0f}}, 5);
        walk(world, player, {}, 3.5f);
        const glm::vec3 settled = levain::physics::bodyPose(world, crate).position;
        walk(world, player, {}, 0.5f);

        CHECK(glm::length(levain::physics::bodyPose(world, crate).position - settled) < 0.001f);
        CHECK(standsAt(feetOf(world, player).y, 0.0f));
        CHECK(glm::length(glm::vec2{feetOf(world, player).x, feetOf(world, player).z}) < 0.001f);
        return settled.y;
    };
    // Le haut de la capsule (1,6 m), sa marge, puis la demi-hauteur de la caisse.
    CHECK(dropped(10.0f) == doctest::Approx(1.6f + Padding + 0.2f).epsilon(0.01));
    // Le haut du corps intérieur (1,52 m), puis la demi-hauteur de la caisse.
    CHECK(dropped(20.0f) == doctest::Approx(1.52f + 0.2f).epsilon(0.01));
}

TEST_CASE("la vitesse du sol se relit après le pas, quand la plateforme a bougé pendant lui")
{
    PhysicsWorld world = worldWithGround();
    const RigidBody kinematic{.motion = levain::physics::Motion::Kinematic};
    const auto platform =
        levain::physics::createBody(world, {.shape = Box{{2.0f, 0.25f, 2.0f}}}, &kinematic,
                                    {.position = {0.0f, 1.0f, 0.0f}}, 6);
    Walker player = spawn(world, {0.0f, 1.25f, 0.0f});
    walk(world, player, {}, 0.25f);

    // La plateforme part à 1 m/s pendant un pas où le personnage ne bouge pas.
    levain::physics::moveKinematic(world, platform, {.position = {Step, 1.0f, 0.0f}}, Step);
    levain::physics::stepPhysics(world, Step);
    CHECK(levain::physics::characterGround(world, player.handle).velocity.x ==
          doctest::Approx(0.0f));
    levain::physics::refreshCharacterGround(world, player.handle);
    CHECK(levain::physics::characterGround(world, player.handle).velocity.x ==
          doctest::Approx(1.0f).epsilon(0.01));
}

TEST_CASE("un volume déclencheur voit son corps intérieur entrer, et l'oublie quand il est détruit")
{
    PhysicsWorld world = worldWithGround();
    constexpr std::uint64_t VolumeEntity = 7;
    levain::physics::createBody(world, {.shape = Box{{1.0f, 1.0f, 1.0f}}, .layer = Layer::Sensor},
                                nullptr, {.position = {0.0f, 1.0f, 0.0f}}, VolumeEntity);
    Walker player = spawn(world, {0.0f, 0.0f, 0.0f});
    walk(world, player, {}, 0.1f);

    std::vector<levain::physics::Overlap> overlaps;
    levain::physics::collectOverlaps(world, overlaps);
    REQUIRE(overlaps.size() == 1);
    CHECK(overlaps[0].volume == VolumeEntity);
    CHECK(overlaps[0].body == PlayerEntity);

    levain::physics::destroyCharacter(world, player.handle);
    levain::physics::collectOverlaps(world, overlaps);
    CHECK(overlaps.empty());
}

TEST_CASE("sur une plateforme qui bouge, la vitesse du sol l'emporte avec elle")
{
    PhysicsWorld world = worldWithGround();
    const RigidBody kinematic{.motion = levain::physics::Motion::Kinematic};
    const auto platform =
        levain::physics::createBody(world, {.shape = Box{{2.0f, 0.25f, 2.0f}}}, &kinematic,
                                    {.position = {0.0f, 1.0f, 0.0f}}, 6);
    Walker player = spawn(world, {0.0f, 1.25f, 0.0f});
    walk(world, player, {}, 0.25f); // qu'il se pose

    // La plateforme avance de 1 m/s en x, une seconde : le personnage, immobile pour le jeu, suit.
    const float start = feetOf(world, player).x;
    for (int i = 1; i <= 60; ++i)
    {
        levain::physics::moveKinematic(
            world, platform, {.position = {static_cast<float>(i) / 60.0f, 1.0f, 0.0f}}, Step);
        walk(world, player, {}, Step);
    }
    CHECK(levain::physics::characterGround(world, player.handle).body == 6);
    CHECK(feetOf(world, player).x - start == doctest::Approx(1.0f).epsilon(0.05));
}

TEST_CASE("les rayons voient son corps intérieur, avec son entité, et un masque peut l'ignorer")
{
    PhysicsWorld world = worldWithGround();
    const Walker player = spawn(world, {0.0f, 0.0f, 0.0f});
    REQUIRE(player.handle.value != CharacterHandle::None);
    const levain::physics::Ray down{.origin = {0.0f, 5.0f, 0.0f}, .direction = {0.0f, -1.0f, 0.0f}};

    const auto hit = levain::physics::raycast(world, down);
    REQUIRE(hit.has_value());
    const levain::physics::RayHit onPlayer = hit.value_or(levain::physics::RayHit{});
    CHECK(onPlayer.entity == PlayerEntity);
    // Le haut du corps intérieur, à 90 % de la capsule autour de son centre (0,8 m) : 0,8 + 0,72.
    CHECK(onPlayer.point.y == doctest::Approx(1.52f).epsilon(0.01));

    const auto past =
        levain::physics::raycast(world, down, levain::physics::maskOf({Layer::Static}));
    REQUIRE(past.has_value());
    CHECK(past.value_or(levain::physics::RayHit{}).entity == GroundEntity);
}

TEST_CASE("téléporté en l'air, il oublie son sol, et détruit, il libère sa place et son corps")
{
    PhysicsWorld world = worldWithGround();
    Walker player = spawn(world, {0.0f, 0.0f, 0.0f});
    walk(world, player, {}, 0.1f);
    REQUIRE(levain::physics::isWalking(levain::physics::characterGround(world, player.handle)));

    walk(world, player, {1.0f, 0.0f, 0.0f}, 0.1f);
    REQUIRE(glm::length(levain::physics::characterVelocity(world, player.handle)) > 0.5f);

    levain::physics::teleportCharacter(world, player.handle, {.position = {20.0f, 10.0f, 0.0f}});
    CHECK(feetOf(world, player).y == doctest::Approx(10.0f));
    CHECK(levain::physics::characterGround(world, player.handle).state == GroundState::InAir);
    CHECK(glm::length(levain::physics::characterVelocity(world, player.handle)) == 0.0f);
    // Son corps intérieur l'a suivi : un rayon le trouve à la nouvelle place, plus à l'ancienne.
    const levain::physics::Ray down{.origin = {20.0f, 15.0f, 0.0f},
                                    .direction = {0.0f, -1.0f, 0.0f}};
    CHECK(levain::physics::raycast(world, down).value_or(levain::physics::RayHit{}).entity ==
          PlayerEntity);
    const levain::physics::Ray oldPlace{.origin = {0.0f, 5.0f, 0.0f},
                                        .direction = {0.0f, -1.0f, 0.0f}};
    CHECK(levain::physics::raycast(world, oldPlace).value_or(levain::physics::RayHit{}).entity ==
          GroundEntity);

    CHECK(levain::physics::bodyCount(world) == 2); // le sol et le corps intérieur
    levain::physics::destroyCharacter(world, player.handle);
    CHECK(levain::physics::characterCount(world) == 0);
    CHECK(levain::physics::bodyCount(world) == 1);
    const Walker next = spawn(world, {0.0f, 0.0f, 0.0f});
    CHECK(next.handle.value == player.handle.value); // la case libre resert
}

TEST_CASE("il ne tourne qu'autour de Y : le tangage d'une rotation de gameplay est ignoré")
{
    PhysicsWorld world = worldWithGround();
    const Walker player = spawn(world, {0.0f, 0.0f, 0.0f});
    // Un quart de tour à gauche, penché de 30° vers l'avant.
    const glm::quat yaw = glm::angleAxis(glm::radians(90.0f), glm::vec3{0.0f, 1.0f, 0.0f});
    const glm::quat pitch = glm::angleAxis(glm::radians(-30.0f), glm::vec3{1.0f, 0.0f, 0.0f});
    levain::physics::turnCharacter(world, player.handle, yaw * pitch);

    const glm::quat turned = levain::physics::characterPose(world, player.handle).rotation;
    const glm::vec3 up = turned * glm::vec3{0.0f, 1.0f, 0.0f};
    const glm::vec3 forward = turned * glm::vec3{0.0f, 0.0f, -1.0f};
    CHECK(up.y == doctest::Approx(1.0f));
    CHECK(forward.x == doctest::Approx(-1.0f)); // vers −x : un quart de tour à gauche
}

TEST_CASE(
    "regarder ses pieds ne lui fait pas faire volte-face : le lacet vient alors de l'axe droit")
{
    PhysicsWorld world = worldWithGround();
    const Walker player = spawn(world, {0.0f, 0.0f, 0.0f});
    // Un quart de tour à gauche, puis la tête baissée à la verticale : l'avant pointe vers le sol.
    const glm::quat yaw = glm::angleAxis(glm::radians(90.0f), glm::vec3{0.0f, 1.0f, 0.0f});
    const glm::quat down = glm::angleAxis(glm::radians(-90.0f), glm::vec3{1.0f, 0.0f, 0.0f});
    levain::physics::turnCharacter(world, player.handle, yaw * down);

    const glm::vec3 forward = levain::physics::characterPose(world, player.handle).rotation *
                              glm::vec3{0.0f, 0.0f, -1.0f};
    CHECK(forward.x == doctest::Approx(-1.0f));
    CHECK(forward.y == doctest::Approx(0.0f));
}

// `StandStill` (ADR-0028) : sans vitesse horizontale demandée, la gravité qu'on lui donne même au
// sol ne le fait pas glisser sur une pente praticable ; avec, il bouge ; et un saut vertical part
// droit. Le `walk` de ce fichier ne lui donne pas la gravité au sol : ici, si.
TEST_CASE("sans vitesse horizontale demandée, il tient sur une pente de 30°, et saute droit")
{
    PhysicsWorld world = worldWithGround();
    addRamp(world, -3.0f, 30.0f);
    // Sur la rampe, en x = −1 : 2 m au-delà de son pied, son dessus y est à 2 tan 30°.
    Walker player = spawn(world, {-1.0f, 2.0f * std::tan(glm::radians(30.0f)) + 0.2f, 0.0f});
    const auto stepWith = [&](const glm::vec3& horizontal, float jump)
    {
        const auto ground = levain::physics::characterGround(world, player.handle);
        player.verticalSpeed =
            (levain::physics::isWalking(ground) ? ground.velocity.y + jump : player.verticalSpeed) -
            Gravity * Step;
        levain::physics::moveCharacter(
            world, player.handle, horizontal + glm::vec3{0.0f, player.verticalSpeed, 0.0f}, Step);
        levain::physics::stepPhysics(world, Step);
        levain::physics::refreshCharacterGround(world, player.handle);
    };
    for (int i = 0; i < 30; ++i)
    {
        stepWith({}, 0.0f);
    }
    const glm::vec3 settled = feetOf(world, player);
    for (int i = 0; i < 180; ++i)
    {
        stepWith({}, 0.0f);
    }
    CHECK(glm::length(feetOf(world, player) - settled) < 0.005f);

    // Un saut vertical : il monte et retombe à sa place, sans glisser.
    stepWith({}, 5.0f);
    float highest = settled.y;
    for (int i = 0; i < 90; ++i)
    {
        stepWith({}, 0.0f);
        highest = std::max(highest, feetOf(world, player).y);
    }
    CHECK(highest > settled.y + 1.0f);
    CHECK(glm::length(feetOf(world, player) - settled) < 0.02f);

    // Avec une vitesse horizontale, il bouge.
    for (int i = 0; i < 30; ++i)
    {
        stepWith({1.0f, 0.0f, 0.0f}, 0.0f);
    }
    CHECK(feetOf(world, player).x > settled.x + 0.3f);
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

// Un refus est une donnée, pas un bug (ADR-0034) : il se teste dans tous les presets.
TEST_CASE("createCharacter refuse une masse nulle : aucun personnage, et le monde reste intact")
{
    PhysicsWorld world = worldWithGround();
    const auto refused = levain::physics::createCharacter(world, {.mass = 0.0f}, {}, PlayerEntity);
    CHECK(refused.value == CharacterHandle::None);
    CHECK(levain::physics::characterCount(world) == 0);
    CHECK(levain::physics::bodyCount(world) == 1);
}
