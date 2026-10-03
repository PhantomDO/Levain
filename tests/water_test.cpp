#include <algorithm>
#include <cmath>
#include <cstdlib>

#include <doctest/doctest.h>

#include "levain/water/lake.hpp"

namespace
{

using levain::assets::Image;

/// La normale du pixel (u, v), ramenée de 0..255 à −1..1.
glm::vec3 normalAt(const Image& map, std::uint32_t u, std::uint32_t v)
{
    const std::size_t offset = ((std::size_t{v} * map.width) + u) * 4;
    return (glm::vec3{map.rgba[offset], map.rgba[offset + 1], map.rgba[offset + 2]} / 255.0f *
            2.0f) -
           1.0f;
}

} // namespace

TEST_CASE("les vaguelettes : des normales unitaires, tournées vers le haut, les mêmes pour la "
          "même graine")
{
    const Image map = levain::water::rippleNormalMapOf(64, 3);
    REQUIRE(map.rgba.size() == std::size_t{64} * 64 * 4);
    float steepest = 1.0f;
    for (std::uint32_t v = 0; v < 64; ++v)
    {
        for (std::uint32_t u = 0; u < 64; ++u)
        {
            const glm::vec3 normal = normalAt(map, u, v);
            CHECK(glm::length(normal) == doctest::Approx(1.0f).epsilon(0.02));
            steepest = std::min(steepest, normal.z);
        }
    }
    // Ni plates ni chaotiques : des pentes de quelques degrés à une vingtaine.
    CHECK(steepest < std::cos(glm::radians(5.0f)));
    CHECK(steepest > std::cos(glm::radians(25.0f)));
    CHECK(levain::water::rippleNormalMapOf(64, 3).rgba == map.rgba);
    CHECK(levain::water::rippleNormalMapOf(64, 4).rgba != map.rgba);
}

TEST_CASE("les vaguelettes se répètent sans couture : d'un bord à l'autre, pas plus d'écart "
          "qu'entre deux pixels voisins")
{
    const Image map = levain::water::rippleNormalMapOf(64, 3);
    float inside = 0.0f;
    float seam = 0.0f;
    for (std::uint32_t v = 0; v < 64; ++v)
    {
        for (std::uint32_t u = 0; u + 1 < 64; ++u)
        {
            inside = std::max(inside, glm::length(normalAt(map, u + 1, v) - normalAt(map, u, v)));
        }
        seam = std::max(seam, glm::length(normalAt(map, 0, v) - normalAt(map, 63, v)));
    }
    CHECK(seam <= inside);
}
