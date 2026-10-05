#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

#include <doctest/doctest.h>
#include <flecs.h>
#include <glm/gtc/quaternion.hpp>

#include "levain/physics/body_rules.hpp"
#include "levain/physics/components.hpp"
#include "levain/physics/layers.hpp"
#include "levain/physics/physics.hpp"
#include "levain/physics/physics_world.hpp"
#include "levain/physics/queries.hpp"
#include "levain/scene/components.hpp"
#include "levain/scene/fixed_step.hpp"
#include "levain/scene/scene.hpp"

using levain::physics::Box;
using levain::physics::Collider;
using levain::physics::Layer;
using levain::physics::Motion;
using levain::physics::PhysicsWorld;
using levain::physics::RigidBody;
using levain::scene::Transform;

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

void advance(flecs::world& world, int steps)
{
    levain::scene::FixedStep step;
    for (int i = 0; i < steps; ++i)
    {
        levain::scene::advanceWorld(world, step, Step);
    }
}

flecs::world physicsWorld()
{
    flecs::world world;
    world.import<levain::physics::PhysicsModule>();
    world.entity("Ground").set(Transform{.position = GroundPose.position}).set(groundCollider());
    return world;
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

TEST_CASE(
    "créer puis détruire un corps le retire du monde, et détruire « aucun corps » ne fait rien")
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

namespace
{

/// Un plan incliné en grille de hauteurs : 9 × 9 échantillons d'un mètre, qui montent de 0,1 m par
/// mètre le long de x.
std::shared_ptr<const levain::physics::HeightField> slope()
{
    auto field = std::make_shared<levain::physics::HeightField>();
    field->size = 9;
    field->spacing = 1.0f;
    for (std::uint32_t z = 0; z < 9; ++z)
    {
        for (std::uint32_t x = 0; x < 9; ++x)
        {
            field->heights.push_back(0.1f * static_cast<float>(x));
        }
    }
    return field;
}

/// Un sol de deux triangles, 10 m de côté, à y = 0, face vers le haut.
std::shared_ptr<const levain::physics::TriangleMesh> floorMesh()
{
    return std::make_shared<const levain::physics::TriangleMesh>(levain::physics::TriangleMesh{
        .vertices =
            {{-5.0f, 0.0f, -5.0f}, {5.0f, 0.0f, -5.0f}, {5.0f, 0.0f, 5.0f}, {-5.0f, 0.0f, 5.0f}},
        .indices = {0, 2, 1, 0, 3, 2}});
}

} // namespace

TEST_CASE("une grande forme vide, trouée, dynamique, mobile ou en capteur est refusée")
{
    using levain::physics::whyNotThisShape;
    const RigidBody dynamic;
    const RigidBody kinematic{.motion = Motion::Kinematic};
    CHECK_FALSE(
        whyNotThisShape(Collider{.shape = levain::physics::HeightFieldShape{slope()}}, nullptr)
            .has_value());
    CHECK_FALSE(
        whyNotThisShape(Collider{.shape = levain::physics::MeshShape{floorMesh()}}, &kinematic)
            .has_value());
    CHECK(whyNotThisShape(Collider{.shape = levain::physics::MeshShape{floorMesh()}}, &dynamic)
              .has_value());
    CHECK(whyNotThisShape(Collider{.shape = levain::physics::MeshShape{nullptr}}, nullptr)
              .has_value());
    CHECK(whyNotThisShape(Collider{.shape = levain::physics::HeightFieldShape{slope()}}, &kinematic)
              .has_value());
    CHECK(whyNotThisShape(
              Collider{.shape = levain::physics::MeshShape{floorMesh()}, .layer = Layer::Sensor},
              nullptr)
              .has_value());
    auto holed = std::make_shared<levain::physics::TriangleMesh>(*floorMesh());
    holed->indices.back() = 99; // un sommet qui n'existe pas
    CHECK(
        whyNotThisShape(Collider{.shape = levain::physics::MeshShape{holed}}, nullptr).has_value());
    auto broken = std::make_shared<levain::physics::HeightField>(*slope());
    broken->heights.pop_back(); // size × size hauteurs, moins une
    CHECK(whyNotThisShape(Collider{.shape = levain::physics::HeightFieldShape{broken}}, nullptr)
              .has_value());
    // Ce que Jolt accepterait, puis dont il ferait une forme fausse ou qu'il refuserait plus tard.
    auto lost = std::make_shared<levain::physics::TriangleMesh>(*floorMesh());
    lost->vertices[1].x = std::numeric_limits<float>::quiet_NaN();
    CHECK(
        whyNotThisShape(Collider{.shape = levain::physics::MeshShape{lost}}, nullptr).has_value());
    const auto tiny =
        std::make_shared<const levain::physics::HeightField>(levain::physics::HeightField{
            .size = 2, .spacing = 1.0f, .heights = {0.0f, 0.0f, 0.0f, 0.0f}});
    CHECK(whyNotThisShape(Collider{.shape = levain::physics::HeightFieldShape{tiny}}, nullptr)
              .has_value());
    auto pierced = std::make_shared<levain::physics::HeightField>(*slope());
    pierced->heights[10] = std::numeric_limits<float>::max(); // le trou de Jolt
    CHECK(whyNotThisShape(Collider{.shape = levain::physics::HeightFieldShape{pierced}}, nullptr)
              .has_value());
    CHECK(whyNotThisShape(
              Collider{.shape = levain::physics::HeightFieldShape{slope()}, .layer = Layer::Sensor},
              nullptr)
              .has_value());
}

TEST_CASE("une caisse posée sur une grille de hauteurs s'arrête à la hauteur de la grille")
{
    PhysicsWorld world = levain::physics::createPhysicsWorld();
    levain::physics::createBody(
        world, Collider{.shape = levain::physics::HeightFieldShape{slope()}}, nullptr, {}, 1);
    const RigidBody body;
    // Une caisse, que son frottement (0,5) retient sur cette pente de 0,1 : posée à plat sur la
    // pente, son centre est à 0,5 / cos θ au-dessus de la surface, verticalement. En x ≠ z : des
    // axes inversés la poseraient à 0,2 au lieu de 0,6.
    const auto crate = levain::physics::createBody(world, crateCollider(), &body,
                                                   {.position = {6.0f, 2.0f, 2.0f}}, 2);
    for (int i = 0; i < 120; ++i)
    {
        levain::physics::stepPhysics(world, Step);
    }
    const glm::vec3 position = levain::physics::bodyPose(world, crate).position;
    CAPTURE(position.x);
    CAPTURE(position.y);
    CHECK(position.x == doctest::Approx(6.0f).epsilon(0.05));
    CHECK(restsOn(position.y, 0.1f * position.x, 0.5f / std::cos(std::atan(0.1f))));
}

TEST_CASE("une caisse tombe sur un sol en maillage et s'y arrête, et deux corps partagent sa forme")
{
    PhysicsWorld world = levain::physics::createPhysicsWorld();
    auto mesh = floorMesh();
    levain::physics::createBody(world, Collider{.shape = levain::physics::MeshShape{mesh}}, nullptr,
                                {}, 1);
    levain::physics::createBody(world, Collider{.shape = levain::physics::MeshShape{mesh}}, nullptr,
                                {.position = {20.0f, 0.0f, 0.0f}}, 2);
    CHECK(levain::physics::sharedShapeCount(world) == 1);
    const RigidBody body;
    const auto crate = levain::physics::createBody(world, crateCollider(), &body,
                                                   {.position = {0.0f, 3.0f, 0.0f}}, 3);
    const auto other = levain::physics::createBody(world, crateCollider(), &body,
                                                   {.position = {20.0f, 3.0f, 0.0f}}, 4);
    for (int i = 0; i < 120; ++i)
    {
        levain::physics::stepPhysics(world, Step);
    }
    CHECK(restsOn(levain::physics::bodyPose(world, crate).position.y, 0.0f, 0.5f));
    CHECK(restsOn(levain::physics::bodyPose(world, other).position.y, 0.0f, 0.5f));

    // Les corps gardent leur forme ; la donnée relâchée, le module oublie la sienne au pas suivant,
    // et une nouvelle donnée a sa propre forme, même si elle reprend l'adresse de l'ancienne.
    mesh.reset();
    levain::physics::stepPhysics(world, Step);
    CHECK(levain::physics::sharedShapeCount(world) == 0);
    CHECK(restsOn(levain::physics::bodyPose(world, crate).position.y, 0.0f, 0.5f));
    levain::physics::createBody(world, Collider{.shape = levain::physics::MeshShape{floorMesh()}},
                                nullptr, {.position = {40.0f, 0.0f, 0.0f}}, 5);
    CHECK(levain::physics::sharedShapeCount(world) == 1);
}

TEST_CASE("un sol en maillage cinématique soulève la caisse posée dessus")
{
    PhysicsWorld world = levain::physics::createPhysicsWorld();
    const RigidBody platform{.motion = Motion::Kinematic};
    // Jolt ne sait pas la masse d'un maillage : sans celle que donne le module, ce corps arrête le
    // test sur une assertion de Jolt en Debug.
    const auto floor = levain::physics::createBody(
        world, Collider{.shape = levain::physics::MeshShape{floorMesh()}}, &platform, {}, 1);
    REQUIRE(floor.value != levain::physics::BodyHandle::None);
    const RigidBody body;
    const auto crate = levain::physics::createBody(world, crateCollider(), &body,
                                                   {.position = {0.0f, 0.5f, 0.0f}}, 2);
    for (int i = 1; i <= 60; ++i)
    {
        levain::physics::moveKinematic(
            world, floor, {.position = {0.0f, static_cast<float>(i) / 60.0f, 0.0f}}, Step);
        levain::physics::stepPhysics(world, Step);
    }
    CHECK(restsOn(levain::physics::bodyPose(world, crate).position.y, 1.0f, 0.5f));
}

TEST_CASE("un rayon touche le premier corps : son entité, le point, la normale, la distance")
{
    PhysicsWorld world = levain::physics::createPhysicsWorld();
    levain::physics::createBody(world, groundCollider(), nullptr, GroundPose, 1);
    levain::physics::createBody(world, crateCollider(), nullptr, {.position = {0.0f, 0.5f, 0.0f}},
                                7);

    // Vers le bas, depuis 10 m : la caisse avant le sol. Son dessus est à y = 1.
    const auto hit = levain::physics::raycast(
        world,
        {.origin = {0.0f, 10.0f, 0.0f}, .direction = {0.0f, -1.0f, 0.0f}, .maxDistance = 100.0f});
    REQUIRE(hit.has_value());
    const levain::physics::RayHit found = hit.value_or(levain::physics::RayHit{});
    CHECK(found.entity == 7);
    CHECK(found.distance == doctest::Approx(9.0f));
    CHECK(found.point.y == doctest::Approx(1.0f));
    CHECK(found.normal.y == doctest::Approx(1.0f));

    // À côté de la caisse : le sol, sa normale vers le haut.
    const auto beside = levain::physics::raycast(
        world, {.origin = {3.0f, 10.0f, 0.0f}, .direction = {0.0f, -2.0f, 0.0f}}); // non normalisée
    CHECK(beside.value_or(levain::physics::RayHit{}).entity == 1);
    CHECK(beside.value_or(levain::physics::RayHit{}).distance == doctest::Approx(10.0f));

    // Trop court, ou vers le ciel : rien.
    CHECK_FALSE(levain::physics::raycast(world, {.origin = {0.0f, 10.0f, 0.0f},
                                                 .direction = {0.0f, -1.0f, 0.0f},
                                                 .maxDistance = 5.0f})
                    .has_value());
    CHECK_FALSE(levain::physics::raycast(
                    world, {.origin = {0.0f, 10.0f, 0.0f}, .direction = {0.0f, 1.0f, 0.0f}})
                    .has_value());
    // Une direction nulle ne lance rien, au lieu d'un rayon de longueur nulle.
    CHECK_FALSE(levain::physics::raycast(
                    world, {.origin = {0.0f, 10.0f, 0.0f}, .direction = {0.0f, 0.0f, 0.0f}})
                    .has_value());
}

TEST_CASE("un rayon ignore les couches hors de son masque, et les volumes déclencheurs par défaut")
{
    PhysicsWorld world = levain::physics::createPhysicsWorld();
    levain::physics::createBody(world, groundCollider(), nullptr, GroundPose, 1);
    levain::physics::createBody(world,
                                Collider{.shape = Box{{2.0f, 2.0f, 2.0f}}, .layer = Layer::Sensor},
                                nullptr, {.position = {0.0f, 3.0f, 0.0f}}, 2);
    const levain::physics::Ray down{.origin = {0.0f, 10.0f, 0.0f},
                                    .direction = {0.0f, -1.0f, 0.0f}};

    CHECK(levain::physics::raycast(world, down).value_or(levain::physics::RayHit{}).entity == 1);
    CHECK(levain::physics::raycast(world, down, levain::physics::maskOf({Layer::Sensor}))
              .value_or(levain::physics::RayHit{})
              .entity == 2);
    CHECK_FALSE(levain::physics::raycast(world, down, levain::physics::maskOf({Layer::Dynamic}))
                    .has_value());
}

TEST_CASE("une sphère lancée s'arrête un rayon avant la surface")
{
    PhysicsWorld world = levain::physics::createPhysicsWorld();
    levain::physics::createBody(world, groundCollider(), nullptr, GroundPose, 1);
    const auto hit = levain::physics::sphereCast(
        world, {.origin = {0.0f, 10.0f, 0.0f}, .direction = {0.0f, -1.0f, 0.0f}}, 0.5f);
    REQUIRE(hit.has_value());
    const levain::physics::RayHit found = hit.value_or(levain::physics::RayHit{});
    CHECK(found.entity == 1);
    CHECK(found.distance == doctest::Approx(9.5f).epsilon(0.001)); // le centre s'arrête à y = 0,5
    CHECK(found.point.y == doctest::Approx(0.0f).scale(1.0f));     // le contact, sur le sol
    CHECK(found.normal.y == doctest::Approx(1.0f));
}

TEST_CASE("un rayon ne voit que les surfaces qu'il traverse en entrant, comme celui d'Unity")
{
    PhysicsWorld world = levain::physics::createPhysicsWorld();
    levain::physics::createBody(world, groundCollider(), nullptr, GroundPose, 1);
    levain::physics::createBody(world, crateCollider(), nullptr, {.position = {0.0f, 0.5f, 0.0f}},
                                7);
    levain::physics::createBody(world,
                                Collider{.shape = levain::physics::HeightFieldShape{slope()}},
                                nullptr, {.position = {20.0f, 0.0f, 0.0f}}, 8);

    // Du centre de la caisse : ni vers le ciel, ni vers le sol, la caisse elle-même ne compte pas.
    CHECK_FALSE(levain::physics::raycast(
                    world, {.origin = {0.0f, 0.5f, 0.0f}, .direction = {0.0f, 1.0f, 0.0f}})
                    .has_value());
    const auto down = levain::physics::raycast(
        world, {.origin = {0.0f, 0.5f, 0.0f}, .direction = {0.0f, -1.0f, 0.0f}});
    CHECK(down.value_or(levain::physics::RayHit{}).entity == 1);
    CHECK(down.value_or(levain::physics::RayHit{}).distance == doctest::Approx(0.5f));

    // La grille, par-dessus, en (6, 2) de son coin : 0,6 m, et non 0,2 comme le dirait une grille
    // lue dans l'autre sens. Par-dessous, elle n'existe pas.
    const auto above = levain::physics::raycast(
        world, {.origin = {26.0f, 10.0f, 2.0f}, .direction = {0.0f, -1.0f, 0.0f}});
    CHECK(above.value_or(levain::physics::RayHit{}).entity == 8);
    // Jolt range les hauteurs sur 16 bits par bloc : 0,6004 m, à moins d'un millimètre près.
    CHECK(above.value_or(levain::physics::RayHit{}).point.y ==
          doctest::Approx(0.6f).epsilon(0.005));
    CHECK_FALSE(levain::physics::raycast(
                    world, {.origin = {26.0f, -0.5f, 2.0f}, .direction = {0.0f, 1.0f, 0.0f}})
                    .has_value());
}

TEST_CASE(
    "une sphère lancée depuis un contact touche à 0 en avançant vers lui, rien en s'éloignant")
{
    PhysicsWorld world = levain::physics::createPhysicsWorld();
    levain::physics::createBody(world, groundCollider(), nullptr, GroundPose, 1);
    // Le centre à 0,3 au-dessus du sol : la sphère de 0,5 y entre de 0,2.
    const auto toward = levain::physics::sphereCast(
        world, {.origin = {0.0f, 0.3f, 0.0f}, .direction = {0.0f, -1.0f, 0.0f}}, 0.5f);
    CHECK(toward.value_or(levain::physics::RayHit{.distance = -1.0f}).distance ==
          doctest::Approx(0.0f));
    CHECK_FALSE(levain::physics::sphereCast(
                    world, {.origin = {0.0f, 0.3f, 0.0f}, .direction = {0.0f, 1.0f, 0.0f}}, 0.5f)
                    .has_value());
}

TEST_CASE("une sphère lancée sur l'arête d'une boîte rend la normale du contact, pas d'une face")
{
    PhysicsWorld world = levain::physics::createPhysicsWorld();
    levain::physics::createBody(world, crateCollider(), nullptr, {.position = {0.0f, 0.5f, 0.0f}},
                                7);
    // Le long de la diagonale qui passe par l'arête du dessus, en x = 0,5 et y = 1.
    const auto hit = levain::physics::sphereCast(
        world, {.origin = {2.5f, 3.0f, 0.0f}, .direction = {-1.0f, -1.0f, 0.0f}}, 0.5f);
    REQUIRE(hit.has_value());
    const glm::vec3 normal = hit.value_or(levain::physics::RayHit{}).normal;
    CHECK(normal.x == doctest::Approx(std::sqrt(0.5f)).epsilon(0.01));
    CHECK(normal.y == doctest::Approx(std::sqrt(0.5f)).epsilon(0.01));
}

TEST_CASE("un corps est une racine sans échelle")
{
    CHECK_FALSE(levain::physics::whyNotABody(Transform{}, false).has_value());
    CHECK(levain::physics::whyNotABody(Transform{}, true).has_value());
    CHECK(levain::physics::whyNotABody(Transform{.scale = {2.0f, 2.0f, 2.0f}}, false).has_value());
    CHECK(levain::physics::whyNotABody(Transform{.rotation = glm::quat{0.0f, 0.0f, 0.0f, 0.0f}},
                                       false)
              .has_value());
}

TEST_CASE("une caisse lâchée tombe sur le sol et s'y arrête")
{
    PhysicsWorld world = levain::physics::createPhysicsWorld();
    levain::physics::createBody(world, groundCollider(), nullptr, GroundPose, 1);
    const RigidBody body{.mass = 20.0f};
    const auto crate = levain::physics::createBody(world, crateCollider(), &body,
                                                   {.position = {0.0f, 5.0f, 0.0f}}, 2);

    std::vector<levain::physics::MovedBody> moved;
    levain::physics::stepPhysics(world, Step);
    levain::physics::collectMovedBodies(world, moved);
    REQUIRE(moved.size() == 1); // la caisse, pas le sol
    CHECK(moved[0].entity == 2);
    CHECK(moved[0].pose.position.y < 5.0f);

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

TEST_CASE("une entité avec Collider et RigidBody tombe, et son Transform suit")
{
    flecs::world world = physicsWorld();
    const flecs::entity crate = world.entity("Crate")
                                    .set(Transform{.position = {0.0f, 3.0f, 0.0f}})
                                    .set(crateCollider())
                                    .set(RigidBody{});

    advance(world, 1);
    CHECK(crate.has<levain::physics::BodyHandle>());
    CHECK(crate.has<levain::scene::PreviousTransform>()); // le rendu l'interpolera
    CHECK(crate.get<Transform>().position.y < 3.0f);

    advance(world, 180);
    CHECK(restsOn(crate.get<Transform>().position.y, 0.0f, 0.5f));
}

TEST_CASE("un Transform posé téléporte le corps")
{
    flecs::world world = physicsWorld();
    const flecs::entity crate = world.entity()
                                    .set(Transform{.position = {0.0f, 0.5f, 0.0f}})
                                    .set(crateCollider())
                                    .set(RigidBody{});
    advance(world, 1);

    crate.set(Transform{.position = {10.0f, 8.0f, 0.0f}});
    const auto pose = levain::physics::bodyPose(world.get<PhysicsWorld>(),
                                                crate.get<levain::physics::BodyHandle>());
    CHECK(pose.position.x == doctest::Approx(10.0f));
    CHECK(pose.position.y == doctest::Approx(8.0f));

    advance(world, 1); // et il retombe de là
    CHECK(crate.get<Transform>().position.x == doctest::Approx(10.0f));
    CHECK(crate.get<Transform>().position.y < 8.0f);
}

TEST_CASE(
    "un Transform posé par le gameplay pendant la simulation téléporte le corps dans le même pas")
{
    // Le vrai cas : un `set` fait dans un système de la phase Simulation. Sans le point de
    // synchronisation avant la phase Physics, son OnSet arriverait après le pas, et la recopie
    // aurait déjà remis le corps où Jolt le voyait.
    flecs::world world = physicsWorld();
    const flecs::entity crate = world.entity("Crate")
                                    .set(Transform{.position = {0.0f, 5.0f, 0.0f}})
                                    .set(crateCollider())
                                    .set(RigidBody{});
    advance(world, 1);
    world.system("Teleport")
        .kind<levain::scene::Simulation>()
        .run([crate](flecs::iter&) { crate.set(Transform{.position = {10.0f, 5.0f, 0.0f}}); });

    advance(world, 1);
    CHECK(crate.get<Transform>().position.x == doctest::Approx(10.0f));
}

TEST_CASE("un Collider ou un RigidBody ajoutés sans valeur font aussi un corps")
{
    // `add` n'émet pas d'OnSet : sans OnAdd, ces entités resteraient sans corps, en silence.
    flecs::world world = physicsWorld();
    const flecs::entity crate = world.entity()
                                    .set(Transform{.position = {0.0f, 3.0f, 0.0f}})
                                    .add<Collider>()
                                    .add<RigidBody>();
    advance(world, 30);
    CHECK(crate.has<levain::physics::BodyHandle>());
    CHECK(crate.get<Transform>().position.y < 3.0f); // dynamique : il tombe
}

TEST_CASE("désactiver la phase Physics met la physique en pause")
{
    flecs::world world = physicsWorld();
    const flecs::entity crate = world.entity()
                                    .set(Transform{.position = {0.0f, 5.0f, 0.0f}})
                                    .set(crateCollider())
                                    .set(RigidBody{});
    advance(world, 1);
    const float before = crate.get<Transform>().position.y;
    world.component<levain::scene::Physics>().disable();
    advance(world, 30);
    CHECK(crate.get<Transform>().position.y == before);
    world.component<levain::scene::Physics>().enable();
    advance(world, 30);
    CHECK(crate.get<Transform>().position.y < before);
}

TEST_CASE("un cinématique suit son Transform et pousse ce qu'il rencontre")
{
    flecs::world world = physicsWorld();
    const flecs::entity pusher = world.entity()
                                     .set(Transform{.position = {-2.0f, 0.5f, 0.0f}})
                                     .set(crateCollider())
                                     .set(RigidBody{.motion = Motion::Kinematic});
    const flecs::entity crate = world.entity()
                                    .set(Transform{.position = {0.0f, 0.5f, 0.0f}})
                                    .set(crateCollider())
                                    .set(RigidBody{});
    // Le gameplay avance le pousseur de 3 m/s, dans la phase Simulation, avant le pas.
    world.system<Transform, const RigidBody>("MovePusher")
        .kind<levain::scene::Simulation>()
        .each(
            [](flecs::iter& it, std::size_t, Transform& transform, const RigidBody& body)
            {
                if (body.motion == Motion::Kinematic)
                {
                    transform.position.x += 3.0f * it.delta_time();
                }
            });

    advance(world, 60); // une seconde : le pousseur passe de -2 à 1, la caisse doit reculer

    CHECK(pusher.get<Transform>().position.x == doctest::Approx(1.0f).epsilon(0.001));
    CHECK(crate.get<Transform>().position.x > 1.5f); // poussée au-delà du pousseur (demi-largeurs)
}

TEST_CASE("retirer le RigidBody fige le corps, retirer le Collider le supprime")
{
    flecs::world world = physicsWorld();
    const flecs::entity crate = world.entity()
                                    .set(Transform{.position = {0.0f, 5.0f, 0.0f}})
                                    .set(crateCollider())
                                    .set(RigidBody{});
    advance(world, 10);
    const PhysicsWorld& physics = world.get<PhysicsWorld>();
    CHECK(levain::physics::bodyCount(physics) == 2);

    crate.remove<RigidBody>();
    advance(world, 1);
    const float frozenAt = crate.get<Transform>().position.y;
    advance(world, 30);
    CHECK(crate.get<Transform>().position.y == frozenAt); // statique : plus rien ne le déplace
    CHECK(levain::physics::bodyCount(physics) == 2);

    crate.remove<Collider>();
    CHECK_FALSE(crate.has<levain::physics::BodyHandle>());
    CHECK(levain::physics::bodyCount(physics) == 1);
}

TEST_CASE("retirer le Transform retire le corps, le remettre le reconstruit")
{
    flecs::world world = physicsWorld();
    const flecs::entity crate = world.entity()
                                    .set(Transform{.position = {0.0f, 5.0f, 0.0f}})
                                    .set(crateCollider())
                                    .set(RigidBody{});
    advance(world, 1);
    const PhysicsWorld& physics = world.get<PhysicsWorld>();
    REQUIRE(levain::physics::bodyCount(physics) == 2);

    crate.remove<Transform>();
    CHECK_FALSE(crate.has<levain::physics::BodyHandle>());
    CHECK(levain::physics::bodyCount(physics) == 1);

    crate.set(Transform{.position = {0.0f, 5.0f, 0.0f}});
    advance(world, 1);
    CHECK(crate.has<levain::physics::BodyHandle>());
    CHECK(levain::physics::bodyCount(physics) == 2);
}

TEST_CASE("détruire l'entité détruit son corps")
{
    flecs::world world = physicsWorld();
    const flecs::entity crate = world.entity()
                                    .set(Transform{.position = {0.0f, 5.0f, 0.0f}})
                                    .set(crateCollider())
                                    .set(RigidBody{});
    advance(world, 1);
    const PhysicsWorld& physics = world.get<PhysicsWorld>();
    CHECK(levain::physics::bodyCount(physics) == 2);

    crate.destruct();
    advance(world, 1);
    CHECK(levain::physics::bodyCount(physics) == 1);
}

TEST_CASE("déplacer le sol réveille la caisse endormie dessus")
{
    flecs::world world = physicsWorld();
    const flecs::entity crate = world.entity()
                                    .set(Transform{.position = {0.0f, 0.5f, 0.0f}})
                                    .set(crateCollider())
                                    .set(RigidBody{});
    advance(world, 120); // deux secondes : Jolt l'endort
    std::vector<levain::physics::MovedBody> moved;
    levain::physics::collectMovedBodies(world.get<PhysicsWorld>(), moved);
    REQUIRE(moved.empty());

    // Le sol descend de 2 m : sans réveil, la caisse resterait suspendue dans le vide.
    world.lookup("Ground").set(Transform{.position = {0.0f, -2.5f, 0.0f}});
    advance(world, 120);
    CHECK(restsOn(crate.get<Transform>().position.y, -2.0f, 0.5f));
}

#if !LEVAIN_ASSERTIONS_ENABLED
// En Debug, le refus s'arrête sur une assertion : il ne se teste qu'en Release, où il doit rester
// bruyant au journal et ne laisser aucun corps.
TEST_CASE("un corps refusé n'existe pas : enfant, ou avec une échelle")
{
    flecs::world world = physicsWorld();
    const flecs::entity parent = world.entity().set(Transform{});
    const flecs::entity child = world.entity(flecs::Parent{parent})
                                    .set(Transform{.position = {0.0f, 3.0f, 0.0f}})
                                    .set(crateCollider())
                                    .set(RigidBody{});
    const flecs::entity scaled = world.entity()
                                     .set(Transform{.scale = {2.0f, 2.0f, 2.0f}})
                                     .set(crateCollider())
                                     .set(RigidBody{});
    advance(world, 1);
    CHECK_FALSE(child.has<levain::physics::BodyHandle>());
    CHECK_FALSE(scaled.has<levain::physics::BodyHandle>());
    CHECK(levain::physics::bodyCount(world.get<PhysicsWorld>()) == 1); // le sol seul

    // Un corps accepté qui prend une échelle plus tard perd aussi le sien.
    const flecs::entity crate =
        world.entity().set(Transform{}).set(crateCollider()).set(RigidBody{});
    advance(world, 1);
    REQUIRE(crate.has<levain::physics::BodyHandle>());
    crate.set(Transform{.scale = {0.5f, 0.5f, 0.5f}});
    advance(world, 1);
    CHECK_FALSE(crate.has<levain::physics::BodyHandle>());

    // Un refus n'est pas définitif : l'échelle revenue à 1, le corps revient.
    crate.set(Transform{});
    advance(world, 1);
    CHECK(crate.has<levain::physics::BodyHandle>());

    // Un corps existant qui prend un parent le perd, et le retrouve quand il redevient une racine.
    crate.set(flecs::Parent{parent});
    advance(world, 1);
    CHECK_FALSE(crate.has<levain::physics::BodyHandle>());
    crate.remove<flecs::Parent>();
    advance(world, 1);
    CHECK(crate.has<levain::physics::BodyHandle>());
}
#endif
