#include <algorithm>

#include <doctest/doctest.h>

#include "levain/grass/grass.hpp"
#include "levain/terrain/layers.hpp"

TEST_CASE("la densité de l'herbe : le poids de sa couche, éteint sous l'eau et sur le rivage")
{
    const levain::terrain::Heightmap valley = levain::terrain::valleyOf({});
    constexpr float WaterLevel = -1.5f;
    const std::vector<std::uint8_t> density = levain::grass::densityMapOf(valley, WaterLevel);
    const std::vector<glm::u8vec4> weights = levain::terrain::weightMapOf(valley);
    REQUIRE(density.size() == valley.heights.size());
    std::size_t underwater = 0;
    std::size_t grassy = 0;
    std::size_t wrong = 0;
    for (std::size_t i = 0; i < density.size(); ++i)
    {
        if (valley.heights[i] < WaterLevel)
        {
            wrong += density[i] != 0 ? 1 : 0;
            ++underwater;
        }
        else if (valley.heights[i] > WaterLevel + 0.3f)
        {
            wrong += density[i] != weights[i].r ? 1 : 0;
            grassy += density[i] > 128 ? 1 : 0;
        }
    }
    CHECK(wrong == 0);
    CHECK(underwater > 1000); // le lac
    CHECK(grassy > 10000);    // le fond de la vallée
}

TEST_CASE("le nombre de brins : tous près de la caméra, puis comme l'inverse du carré de la "
          "distance, et aucun au-delà de la portée")
{
    const levain::grass::GrassSettings settings;
    const float area = 32.0f * 32.0f;
    const auto full = static_cast<std::uint32_t>(settings.bladesPerSquareMeter * area);
    CHECK(levain::grass::bladeCountOf(0.0f, area, settings) == full);
    CHECK(levain::grass::bladeCountOf(settings.fullDensityDistance, area, settings) == full);
    CHECK(levain::grass::bladeCountOf(2.0f * settings.fullDensityDistance, area, settings) ==
          full / 4);
    CHECK(levain::grass::bladeCountOf(settings.range, area, settings) == 0);
}

TEST_CASE("un brin : des trapèzes qui s'affinent de la base à la pointe")
{
    const levain::grass::BladeGeometry blade = levain::grass::bladeGeometryOf(3);
    CHECK(blade.vertices.size() == 9);
    CHECK(blade.indices.size() == (3 * 6) + 3);
    CHECK(blade.vertices.front() == glm::vec2{-0.5f, 0.0f});
    CHECK(blade.vertices.back() == glm::vec2{0.0f, 1.0f});
    CHECK(std::ranges::all_of(blade.indices,
                              [&](std::uint16_t index) { return index < blade.vertices.size(); }));
    // Chaque rangée moins large que la précédente.
    for (std::size_t row = 1; row + 1 < blade.vertices.size(); row += 2)
    {
        CHECK(blade.vertices[row].x > 0.0f);
        if (row + 2 < blade.vertices.size() - 1)
        {
            CHECK(blade.vertices[row + 2].x < blade.vertices[row].x);
        }
    }
}
