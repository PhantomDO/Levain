#include "levain/terrain/layers.hpp"

#include <algorithm>
#include <cmath>

namespace levain::terrain
{

glm::vec3 layerWeightsOf(float slope, float elevation)
{
    // La roche d'abord, là où c'est trop raide pour que rien tienne ; le reste se partage entre
    // l'herbe en bas et le sol rocailleux en haut.
    const float rock = glm::smoothstep(0.12f, 0.3f, slope); // de 28° à 45°
    const float stony = glm::smoothstep(0.35f, 0.7f, elevation);
    return {(1.0f - rock) * (1.0f - stony), (1.0f - rock) * stony, rock};
}

std::vector<glm::u8vec4> weightMapOf(const Heightmap& heightmap)
{
    const auto [lowest, highest] = std::ranges::minmax(heightmap.heights);
    const float range = std::max(highest - lowest, 1e-3f);
    std::vector<glm::u8vec4> weights(heightmap.heights.size());
    for (std::uint32_t z = 0; z < heightmap.size; ++z)
    {
        for (std::uint32_t x = 0; x < heightmap.size; ++x)
        {
            const glm::vec2 position = glm::vec2{x, z} * heightmap.spacing;
            const std::size_t index = (std::size_t{z} * heightmap.size) + x;
            const float slope = 1.0f - normalAt(heightmap, position).y;
            const glm::vec3 weight =
                layerWeightsOf(slope, (heightmap.heights[index] - lowest) / range);
            weights[index] = glm::u8vec4{glm::round(weight * 255.0f), 0};
        }
    }
    return weights;
}

} // namespace levain::terrain
