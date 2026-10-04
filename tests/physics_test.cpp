#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include <doctest/doctest.h>
#include <glm/gtc/quaternion.hpp>

#include "levain/physics/body_rules.hpp"
#include "levain/physics/components.hpp"
#include "levain/physics/layers.hpp"
#include "levain/physics/physics_world.hpp"

using levain::physics::Box;
using levain::physics::Collider;
using levain::physics::Layer;
using levain::physics::PhysicsWorld;
using levain::physics::RigidBody;

namespace
{

constexpr float Step = 1.0f / 60.0f;

/// Un sol de 100 m × 100 m dont le dessus est à y = 0. Des fonctions et non des constantes
/// globales : un `std::variant` construit avant `main` pourrait lever sans qu'on le rattrape.
Collider groundCollider()
{
    return {.shape = Box{{50.0f, 0.5f, 50.0f}}};
}

constexpr levain::physics::BodyPose GroundPose{.position = {0.0f, -0.5f, 0.0f}};

/// Une caisse d'un mètre : posée sur le sol, son centre est à y = 0,5.
Collider crateCollider()
{
    return {.shape = Box{{0.5f, 0.5f, 0.5f}}};
}

/// Un corps au repos s'enfonce un peu dans ce qui le porte : jusqu'à 2 cm, la *penetration slop*
/// de Jolt (`PhysicsSettings::mPenetrationSlop`), qu'il laisse pour que les piles ne tremblent pas.
constexpr float PenetrationSlop = 0.02f;

/// Vrai si un centre à `y` est celui d'un corps de demi-hauteur `halfHeight` posé sur `surface`.
bool restsOn(float y, float surface, float halfHeight)
{
    const float expected = surface + halfHeight;
    return y <= expected + 0.001f && y >= expected - PenetrationSlop - 0.001f;
}

} // namespace

TEST_CASE("la matrice des couches est symétrique")
{
    for (std::uint8_t first = 0; first < levain::physics::LayerCount; ++first)
    {
        for (std::uint8_t second = 0; second < levain::physics::LayerCount; ++second)
        {
            CAPTURE(first);
            CAPTURE(second);
            CHECK(levain::physics::layersCollide(static_cast<Layer>(first),
                                                 static_cast<Layer>(second)) ==
                  levain::physics::layersCollide(static_cast<Layer>(second),
                                                 static_cast<Layer>(first)));
        }
    }
}

TEST_CASE("la matrice des couches est celle de l'ADR-0026, case par case")
{
    // Recopiée de la table de l'ADR : une erreur symétrique passerait le test de symétrie.
    constexpr bool O = true;
    constexpr bool _ = false;
    constexpr std::array<std::array<bool, levain::physics::LayerCount>, levain::physics::LayerCount>
        Expected{{
            // Static Dynamic Character Sensor Debris
            {_, O, O, _, O}, // Static
            {O, O, O, O, O}, // Dynamic
            {O, O, _, O, _}, // Character
            {_, O, O, _, _}, // Sensor
            {O, O, _, _, _}, // Debris
        }};
    for (std::uint8_t first = 0; first < levain::physics::LayerCount; ++first)
    {
        for (std::uint8_t second = 0; second < levain::physics::LayerCount; ++second)
        {
            CAPTURE(first);
            CAPTURE(second);
            CHECK(levain::physics::layersCollide(static_cast<Layer>(first),
                                                 static_cast<Layer>(second)) ==
                  Expected[first][second]);
        }
    }
}

TEST_CASE("sans couche donnée, un corps prend celle de son mouvement")
{
    const RigidBody dynamic;
    CHECK(levain::physics::effectiveLayer(crateCollider(), nullptr) == Layer::Static);
    CHECK(levain::physics::effectiveLayer(crateCollider(), &dynamic) == Layer::Dynamic);
    const Collider debris{.shape = Box{}, .layer = Layer::Debris};
    CHECK(levain::physics::effectiveLayer(debris, &dynamic) == Layer::Debris);
}

TEST_CASE(
    "une forme sans épaisseur ou une masse nulle est refusée, avant que Jolt n'en fasse des NaN")
{
    const RigidBody dynamic;
    using levain::physics::whyNotThisShape;
    CHECK_FALSE(whyNotThisShape(crateCollider(), &dynamic).has_value());
    CHECK(whyNotThisShape(Collider{.shape = Box{{1.0f, 0.0f, 1.0f}}}, nullptr).has_value());
    CHECK(whyNotThisShape(Collider{.shape = levain::physics::Sphere{.radius = -1.0f}}, nullptr)
              .has_value());
    CHECK(whyNotThisShape(
              Collider{.shape = levain::physics::Capsule{.halfHeight = 0.0f, .radius = 0.5f}},
              nullptr)
              .has_value());
    const RigidBody weightless{.mass = 0.0f};
    CHECK(whyNotThisShape(crateCollider(), &weightless).has_value());
    // Un cinématique n'a pas besoin de masse : rien ne le pousse.
    const RigidBody kinematic{.motion = levain::physics::Motion::Kinematic, .mass = 0.0f};
    CHECK_FALSE(whyNotThisShape(crateCollider(), &kinematic).has_value());
}

TEST_CASE("un corps mobile sur la couche Static est refusé, il traverserait le décor")
{
    const RigidBody dynamic;
    const Collider onStatic{.shape = Box{}, .layer = Layer::Static};
    CHECK(levain::physics::whyNotThisLayer(onStatic, &dynamic).has_value());
    CHECK_FALSE(levain::physics::whyNotThisLayer(onStatic, nullptr).has_value());
    CHECK_FALSE(levain::physics::whyNotThisLayer(crateCollider(), &dynamic).has_value());
}

TEST_CASE("créer puis détruire un corps le retire du monde ; détruire « aucun corps » ne fait rien")
{
    PhysicsWorld world = levain::physics::createPhysicsWorld();
    const auto ground =
        levain::physics::createBody(world, groundCollider(), nullptr, GroundPose, 1);
    CHECK(levain::physics::bodyCount(world) == 1);
    // Jolt ne vérifie pas : retirer le BodyID invalide lirait hors de son tableau de corps.
    levain::physics::destroyBody(world, levain::physics::BodyHandle{});
    CHECK(levain::physics::bodyCount(world) == 1);
    levain::physics::destroyBody(world, ground);
    CHECK(levain::physics::bodyCount(world) == 0);
}

TEST_CASE("une rotation donnée à un corps est celle qu'il rend, dans le bon ordre des composantes")
{
    PhysicsWorld world = levain::physics::createPhysicsWorld();
    const glm::quat rotation = glm::angleAxis(0.5f, glm::normalize(glm::vec3{1.0f, 2.0f, 3.0f}));
    const auto body = levain::physics::createBody(
        world, crateCollider(), nullptr, {.position = {1.0f, 2.0f, 3.0f}, .rotation = rotation}, 1);
    const glm::quat back = levain::physics::bodyPose(world, body).rotation;
    CHECK(std::abs(glm::dot(back, rotation)) == doctest::Approx(1.0f));
}

TEST_CASE("une poutre couchée par sa rotation repose sur sa longueur")
{
    // Un aller-retour ne suffit pas : deux inversions w/x symétriques s'y annuleraient. Une poutre
    // de 4 m tournée de 90° autour de Z se tient debout : son centre doit finir à 2 m du sol.
    PhysicsWorld world = levain::physics::createPhysicsWorld();
    levain::physics::createBody(world, groundCollider(), nullptr, GroundPose, 1);
    const RigidBody body;
    const auto beam = levain::physics::createBody(
        world, Collider{.shape = Box{{2.0f, 0.25f, 0.25f}}}, &body,
        {.position = {0.0f, 2.5f, 0.0f},
         .rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3{0.0f, 0.0f, 1.0f})},
        2);
    for (int i = 0; i < 60; ++i)
    {
        levain::physics::stepPhysics(world, Step);
    }
    CHECK(restsOn(levain::physics::bodyPose(world, beam).position.y, 0.0f, 2.0f));
}

TEST_CASE("une caisse Debris traverse une dalle Debris et s'arrête sur le sol")
{
    // La matrice branchée dans Jolt, et non seulement la fonction : Debris ne touche pas Debris.
    PhysicsWorld world = levain::physics::createPhysicsWorld();
    levain::physics::createBody(world, groundCollider(), nullptr, GroundPose, 1);
    levain::physics::createBody(world,
                                Collider{.shape = Box{{5.0f, 0.1f, 5.0f}}, .layer = Layer::Debris},
                                nullptr, {.position = {0.0f, 2.0f, 0.0f}}, 2);
    const RigidBody body;
    const auto crate =
        levain::physics::createBody(world, Collider{.shape = Box{}, .layer = Layer::Debris}, &body,
                                    {.position = {0.0f, 4.0f, 0.0f}}, 3);
    for (int i = 0; i < 180; ++i)
    {
        levain::physics::stepPhysics(world, Step);
    }
    CHECK(restsOn(levain::physics::bodyPose(world, crate).position.y, 0.0f, 0.5f));
}

TEST_CASE("une caisse lâchée tombe sur le sol et s'y arrête")
{
    PhysicsWorld world = levain::physics::createPhysicsWorld();
    levain::physics::createBody(world, groundCollider(), nullptr, GroundPose, 1);
    const RigidBody body{.mass = 20.0f};
    const auto crate = levain::physics::createBody(world, crateCollider(), &body,
                                                   {.position = {0.0f, 5.0f, 0.0f}}, 2);

    for (int i = 0; i < 180; ++i) // 3 s : la chute de 4,5 m prend moins d'une seconde
    {
        levain::physics::stepPhysics(world, Step);
    }
    CHECK(restsOn(levain::physics::bodyPose(world, crate).position.y, 0.0f, 0.5f));
}

TEST_CASE("la physique donne le même état au bit près, sur un thread ou sur trois")
{
    // Une pile de caisses qui s'écroule : des contacts entre corps dynamiques, résolus en
    // parallèle.
    const auto simulate = [](int workerThreads)
    {
        PhysicsWorld world = levain::physics::createPhysicsWorld({.workerThreads = workerThreads});
        levain::physics::createBody(world, groundCollider(), nullptr, GroundPose, 0);
        std::vector<levain::physics::BodyHandle> crates;
        crates.reserve(40);
        const RigidBody body;
        for (int i = 0; i < 40; ++i)
        {
            const levain::physics::BodyPose pose{.position = {0.3f * static_cast<float>(i % 3),
                                                              0.5f + 1.1f * static_cast<float>(i),
                                                              0.2f * static_cast<float>(i % 2)}};
            crates.push_back(levain::physics::createBody(world, crateCollider(), &body, pose,
                                                         static_cast<std::uint64_t>(i) + 1));
        }
        for (int i = 0; i < 240; ++i)
        {
            levain::physics::stepPhysics(world, Step);
        }
        std::vector<levain::physics::BodyPose> poses;
        poses.reserve(crates.size());
        for (const auto crate : crates)
        {
            poses.push_back(levain::physics::bodyPose(world, crate));
        }
        return poses;
    };

    // Sous WebAssembly, workerThreads est ignoré : les deux tournent sur un seul thread, et le test
    // ne vérifie plus que la répétabilité.
    const auto single = simulate(0);
    const auto threaded = simulate(3);
    REQUIRE(single.size() == threaded.size());
    // Le test ne doit pas passer à vide : la pile s'est écroulée (la plus haute partait de 43 m),
    // des caisses ont tourné, et rien n'est NaN (deux NaN identiques passeraient le memcmp).
    float highest = 0.0f;
    bool anyTurned = false;
    bool allFinite = true;
    for (const auto& pose : single)
    {
        highest = std::max(highest, pose.position.y);
        anyTurned = anyTurned || std::abs(pose.rotation.w) < 0.99f;
        for (const float value :
             {pose.position.x, pose.position.y, pose.position.z, pose.rotation.x, pose.rotation.y,
              pose.rotation.z, pose.rotation.w})
        {
            allFinite = allFinite && std::isfinite(value);
        }
    }
    CHECK(highest < 20.0f);
    CHECK(anyTurned);
    CHECK(allFinite);
    // memcmp et non == : le déterminisme se juge au bit, pas à un epsilon près.
    CHECK(std::memcmp(single.data(), threaded.data(),
                      single.size() * sizeof(levain::physics::BodyPose)) == 0);
}

#if !LEVAIN_ASSERTIONS_ENABLED
// En Debug, le refus s'arrête sur une assertion : il ne se teste qu'en Release, où il doit laisser
// le monde intact et rendre « aucun corps ».
TEST_CASE("createBody refuse une masse nulle : aucun corps, et le monde reste intact")
{
    PhysicsWorld world = levain::physics::createPhysicsWorld();
    const RigidBody weightless{.mass = 0.0f};
    const auto refused = levain::physics::createBody(world, crateCollider(), &weightless, {}, 1);
    CHECK(refused.value == levain::physics::BodyHandle::None);
    CHECK(levain::physics::bodyCount(world) == 0);
}
#endif
