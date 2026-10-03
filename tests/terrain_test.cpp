#include <algorithm>
#include <cmath>
#include <set>

#include <doctest/doctest.h>

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
