#include "levain/grass/grass.hpp"

#include <algorithm>
#include <cmath>

#include "levain/terrain/layers.hpp"

namespace levain::grass
{

namespace
{

/// La hauteur au-dessus de l'eau où l'herbe atteint sa densité : en dessous, un rivage nu.
constexpr float ShoreWidth = 0.3f;

} // namespace

std::vector<std::uint8_t> densityMapOf(const terrain::Heightmap& heightmap, float waterLevel)
{
    const std::vector<glm::u8vec4> weights = terrain::weightMapOf(heightmap);
    std::vector<std::uint8_t> density(weights.size());
    for (std::size_t i = 0; i < weights.size(); ++i)
    {
        const float shore =
            glm::smoothstep(waterLevel, waterLevel + ShoreWidth, heightmap.heights[i]);
        density[i] =
            static_cast<std::uint8_t>(std::lround(static_cast<float>(weights[i].r) * shore));
    }
    return density;
}

std::uint32_t bladeCountOf(float distance, float area, const GrassSettings& settings)
{
    if (distance >= settings.range)
    {
        return 0;
    }
    const float near = std::max(distance, settings.fullDensityDistance);
    const float falloff =
        (settings.fullDensityDistance * settings.fullDensityDistance) / (near * near);
    return static_cast<std::uint32_t>(settings.bladesPerSquareMeter * area * falloff);
}

BladeGeometry bladeGeometryOf(std::uint32_t segments)
{
    BladeGeometry blade;
    for (std::uint32_t row = 0; row <= segments; ++row)
    {
        const float t = static_cast<float>(row) / static_cast<float>(segments + 1);
        // Le brin s'affine vers sa pointe : sa largeur décroît avec la hauteur.
        const float halfWidth = 0.5f * (1.0f - t);
        blade.vertices.push_back({-halfWidth, t});
        blade.vertices.push_back({halfWidth, t});
    }
    blade.vertices.push_back({0.0f, 1.0f}); // la pointe
    for (std::uint32_t row = 0; row < segments; ++row)
    {
        const auto left = static_cast<std::uint16_t>(2 * row);
        const auto right = static_cast<std::uint16_t>(left + 1);
        const auto upLeft = static_cast<std::uint16_t>(left + 2);
        const auto upRight = static_cast<std::uint16_t>(left + 3);
        blade.indices.insert(blade.indices.end(), {left, right, upRight, left, upRight, upLeft});
    }
    const auto lastLeft = static_cast<std::uint16_t>(2 * segments);
    const auto tip = static_cast<std::uint16_t>(blade.vertices.size() - 1);
    blade.indices.insert(blade.indices.end(),
                         {lastLeft, static_cast<std::uint16_t>(lastLeft + 1), tip});
    return blade;
}

} // namespace levain::grass
