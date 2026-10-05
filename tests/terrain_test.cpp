#include <algorithm>
#include <cmath>
#include <set>

#include <doctest/doctest.h>
#include <flecs.h>

#include "lake_shore.hpp"

#include "levain/physics/physics.hpp"
#include "levain/physics/physics_world.hpp"
#include "levain/scene/fixed_step.hpp"
#include "levain/scene/scene.hpp"
#include "levain/terrain/collision.hpp"
#include "levain/terrain/heightmap.hpp"
#include "levain/terrain/layers.hpp"
#include "levain/terrain/patches.hpp"

namespace
{

using levain::terrain::Heightmap;

/// Un petit terrain en pente douce selon x : h = 0,5 x.
Heightmap rampOf(std::uint32_t size)
{
    Heightmap ramp{.size = size, .spacing = 1.0f, .heights = {}};
    for (std::uint32_t z = 0; z < size; ++z)
    {
        for (std::uint32_t x = 0; x < size; ++x)
        {
            ramp.heights.push_back(0.5f * static_cast<float>(x));
        }
    }
    return ramp;
}

} // namespace

TEST_CASE("la vallée : 512 m de côté, un fond plus bas que les crêtes, la même pour la même graine")
{
    const levain::terrain::ValleySettings settings;
    const Heightmap valley = levain::terrain::valleyOf(settings);
    CHECK(levain::terrain::extentOf(valley) == doctest::Approx(512.0f));
    CHECK(valley.heights.size() == std::size_t{513} * 513);
    const float center = levain::terrain::heightAt(valley, {256.0f, 256.0f});
    const float corner = levain::terrain::heightAt(valley, {10.0f, 10.0f});
    CHECK(corner > center + 40.0f);
    CHECK(levain::terrain::valleyOf(settings).heights == valley.heights);
    CHECK(levain::terrain::valleyOf({.seed = 7}).heights != valley.heights);
}

TEST_CASE(
    "le lac creuse le fond de sa profondeur au centre, et le laisse intact au-delà de son rayon")
{
    const levain::terrain::ValleySettings settings;
    const Heightmap valley = levain::terrain::valleyOf(settings);
    const Heightmap withoutLake = levain::terrain::valleyOf({.lakeDepth = 0.0f});
    CHECK(levain::terrain::heightAt(valley, settings.lakeCenter) ==
          doctest::Approx(levain::terrain::heightAt(withoutLake, settings.lakeCenter) -
                          settings.lakeDepth)
              .epsilon(0.01));
    const glm::vec2 beyond = settings.lakeCenter - glm::vec2{settings.lakeRadius + 1.0f, 0.0f};
    CHECK(levain::terrain::heightAt(valley, beyond) ==
          doctest::Approx(levain::terrain::heightAt(withoutLake, beyond)));
}

TEST_CASE("la hauteur se lit entre les échantillons, et la normale suit la pente")
{
    const Heightmap ramp = rampOf(33);
    CHECK(levain::terrain::heightAt(ramp, {4.0f, 9.0f}) == doctest::Approx(2.0f));
    CHECK(levain::terrain::heightAt(ramp, {4.5f, 9.25f}) == doctest::Approx(2.25f));
    CHECK(levain::terrain::heightAt(ramp, {-5.0f, 0.0f}) == doctest::Approx(0.0f)); // le bord
    const glm::vec3 normal = levain::terrain::normalAt(ramp, {10.0f, 10.0f});
    CHECK(normal.x == doctest::Approx(-0.5f / std::sqrt(1.25f)));
    CHECK(normal.y == doctest::Approx(1.0f / std::sqrt(1.25f)));
    CHECK(normal.z == doctest::Approx(0.0f));
}

TEST_CASE(
    "le niveau de détail croît d'un cran à chaque doublement de la distance, jusqu'au dernier")
{
    using levain::terrain::lodOf;
    CHECK(lodOf(10.0f, 40.0f) == 0);
    CHECK(lodOf(40.0f, 40.0f) == 0);
    CHECK(lodOf(41.0f, 40.0f) == 1);
    CHECK(lodOf(100.0f, 40.0f) == 2);
    CHECK(lodOf(1e6f, 40.0f) == levain::terrain::MaxLod);
}

TEST_CASE("les parcelles couvrent le terrain une fois chacune, la plus proche au niveau 0")
{
    const Heightmap valley = levain::terrain::valleyOf({});
    const auto patches = levain::terrain::patchesFor(valley, {16.0f, 100.0f, 16.0f}, 40.0f);
    CHECK(patches.size() == std::size_t{16} * 16);
    std::set<std::pair<std::uint32_t, std::uint32_t>> cells;
    for (const auto& patch : patches)
    {
        cells.insert({patch.cell.x, patch.cell.y});
    }
    CHECK(cells.size() == patches.size());
    const auto nearest = std::ranges::find_if(patches, [](const auto& patch)
                                              { return patch.cell == glm::uvec2{0, 0}; });
    REQUIRE(nearest != patches.end());
    CHECK(nearest->lod <= 1);
    const auto farthest = std::ranges::find_if(patches, [](const auto& patch)
                                               { return patch.cell == glm::uvec2{15, 15}; });
    REQUIRE(farthest != patches.end());
    CHECK(farthest->lod == levain::terrain::MaxLod);
}

TEST_CASE("la boîte d'une parcelle contient chacune de ses hauteurs, et touche la plus haute et la "
          "plus basse")
{
    const Heightmap valley = levain::terrain::valleyOf({});
    const glm::uvec2 cell{3, 5};
    const levain::render::Box box = levain::terrain::patchBoundsOf(valley, cell);
    CHECK(box.min.x == doctest::Approx(96.0f));
    CHECK(box.max.z == doctest::Approx(192.0f));
    float low = 1e9f;
    float high = -1e9f;
    for (std::uint32_t z = 160; z <= 192; ++z)
    {
        for (std::uint32_t x = 96; x <= 128; ++x)
        {
            const float height = valley.heights[(std::size_t{z} * valley.size) + x];
            low = std::min(low, height);
            high = std::max(high, height);
        }
    }
    CHECK(box.min.y == low);
    CHECK(box.max.y == high);
}

TEST_CASE("les poids des couches font 1 : l'herbe au fond et à plat, la roche dans les pentes")
{
    const glm::vec3 flat = levain::terrain::layerWeightsOf(0.0f, 0.0f);
    const glm::vec3 cliff = levain::terrain::layerWeightsOf(0.6f, 0.3f);
    const glm::vec3 ridge = levain::terrain::layerWeightsOf(0.05f, 1.0f);
    for (const glm::vec3& weights :
         {flat, cliff, ridge, levain::terrain::layerWeightsOf(0.35f, 0.5f)})
    {
        CHECK(weights.x + weights.y + weights.z == doctest::Approx(1.0f));
    }
    CHECK(flat.x == doctest::Approx(1.0f));  // herbe
    CHECK(cliff.z == doctest::Approx(1.0f)); // roche
    CHECK(ridge.y == doctest::Approx(1.0f)); // sol rocailleux
}

TEST_CASE("la grille d'une parcelle et sa jupe : chaque bord doublé en dessous, la grille tournée "
          "vers le haut")
{
    constexpr std::uint32_t Quads = 4;
    const levain::terrain::PatchGeometry geometry = levain::terrain::patchGeometryOf(Quads);
    // 5 × 5 sommets de grille, et un sommet de jupe sous chacun des 16 sommets du tour.
    CHECK(geometry.vertices.size() == (std::size_t{5} * 5) + 16);
    // 2 triangles par carré, et 2 par segment du tour.
    CHECK(geometry.indices.size() == ((std::size_t{4} * 4 * 2) + (std::size_t{16} * 2)) * 3);
    std::size_t skirts = 0;
    for (const levain::terrain::GridVertex& vertex : geometry.vertices)
    {
        if (vertex.skirt == 1.0f)
        {
            ++skirts;
            // Une jupe est sous un bord : x ou z vaut 0 ou 1.
            const bool onEdge = vertex.local.x == 0.0f || vertex.local.x == 1.0f ||
                                vertex.local.y == 0.0f || vertex.local.y == 1.0f;
            CHECK(onEdge);
        }
    }
    CHECK(skirts == 16);
    // Le premier triangle de la grille, vu d'en haut, a pour normale +Y.
    const auto at = [&](std::size_t i)
    {
        const glm::vec2 local = geometry.vertices[geometry.indices[i]].local;
        return glm::vec3{local.x, 0.0f, local.y};
    };
    const glm::vec3 normal = glm::cross(at(1) - at(0), at(2) - at(0));
    CHECK(normal.y > 0.0f);
}

TEST_CASE("un corps lâché sur la vallée tombe sur le terrain et s'y arrête (critère de #175)")
{
    const levain::terrain::Heightmap valley = levain::terrain::valleyOf({});
    levain::physics::PhysicsWorld world = levain::physics::createPhysicsWorld();
    levain::physics::createBody(world, levain::terrain::colliderOf(valley), nullptr, {}, 1);

    // Au fond plat de la vallée, loin du lac : une boule qui ne roule pas loin.
    const glm::vec2 spot{180.0f, 200.0f};
    const float ground = levain::terrain::heightAt(valley, spot);
    const levain::physics::RigidBody body;
    const auto ball = levain::physics::createBody(
        world, levain::physics::Collider{.shape = levain::physics::Sphere{.radius = 0.5f}}, &body,
        {.position = {spot.x, ground + 20.0f, spot.y}}, 2);
    for (int i = 0; i < 240; ++i) // 4 s : la chute de 20 m en prend 2
    {
        levain::physics::stepPhysics(world, 1.0f / 60.0f);
    }
    const glm::vec3 position = levain::physics::bodyPose(world, ball).position;
    CAPTURE(position.x);
    CAPTURE(position.y);
    CAPTURE(position.z);
    // Sur le terrain, là où il est, et non dessous : la surface sous la boule, plus son rayon, à
    // 2 cm près (la penetration slop de Jolt) et à la pente près.
    const float surface = levain::terrain::heightAt(valley, {position.x, position.z});
    CHECK(position.y > surface + 0.4f);
    CHECK(position.y < surface + 0.6f);
}

TEST_CASE("les caisses de la démo roulent de la rive est jusque dans le lac, un volume déclencheur")
{
    // La scène de `levain_sandbox --view terrain`, sans le rendu : la CI n'y fait que 2 images par
    // seconde sous lavapipe, trop peu pour que les caisses atteignent l'eau.
    flecs::world world;
    world.import<levain::physics::PhysicsModule>();
    const levain::terrain::ValleySettings valley;
    levain::sandbox::spawnLakeShoreCrates(world, levain::terrain::valleyOf(valley), valley);
    const flecs::entity lake = world.lookup("lac");
    REQUIRE(lake);
    levain::scene::FixedStep step;
    for (int i = 0; i < 600; ++i) // 10 s : les 64 y sont (mesuré en Debug), 25 au bout de 6 s
    {
        levain::scene::advanceWorld(world, step, 1.0f / 60.0f);
    }
    // La moitié au moins : le chiffre exact peut varier d'un compilateur à l'autre, pas l'ordre de
    // grandeur. Un terrain qui ne collisionne pas les ferait passer dessous, un volume muet n'en
    // compterait aucune.
    const auto swimmers = levain::physics::occupantsOf(world, lake).size();
    CAPTURE(swimmers);
    CHECK(swimmers >= 32);
}
