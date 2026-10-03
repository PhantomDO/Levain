#include "levain/terrain/patches.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "levain/core/assert.hpp"

namespace levain::terrain
{

std::uint32_t lodOf(float distance, float lod0Distance)
{
    if (!(distance > lod0Distance))
    {
        return 0;
    }
    const auto level =
        static_cast<std::uint32_t>(std::floor(std::log2(distance / lod0Distance))) + 1;
    return std::min(level, MaxLod);
}

std::uint32_t patchesPerSide(const Heightmap& heightmap)
{
    LEVAIN_ASSERT(heightmap.size > 1 && (heightmap.size - 1) % PatchQuads == 0,
                  "un terrain fait un nombre entier de parcelles de côté");
    return (heightmap.size - 1) / PatchQuads;
}

std::vector<Patch> patchesFor(const Heightmap& heightmap, glm::vec3 camera, float lod0Distance)
{
    const std::uint32_t side = patchesPerSide(heightmap);
    const float patchSize = static_cast<float>(PatchQuads) * heightmap.spacing;
    std::vector<Patch> patches;
    patches.reserve(std::size_t{side} * side);
    for (std::uint32_t z = 0; z < side; ++z)
    {
        for (std::uint32_t x = 0; x < side; ++x)
        {
            const glm::vec2 center = (glm::vec2{x, z} + 0.5f) * patchSize;
            const glm::vec3 point{center.x, heightAt(heightmap, center), center.y};
            patches.push_back(
                {.cell = {x, z}, .lod = lodOf(glm::distance(point, camera), lod0Distance)});
        }
    }
    return patches;
}

render::Box patchBoundsOf(const Heightmap& heightmap, glm::uvec2 cell)
{
    float low = std::numeric_limits<float>::max();
    float high = std::numeric_limits<float>::lowest();
    const glm::uvec2 first = cell * PatchQuads;
    for (std::uint32_t z = first.y; z <= first.y + PatchQuads; ++z)
    {
        for (std::uint32_t x = first.x; x <= first.x + PatchQuads; ++x)
        {
            const float height = heightmap.heights[(std::size_t{z} * heightmap.size) + x];
            low = std::min(low, height);
            high = std::max(high, height);
        }
    }
    const glm::vec2 min = glm::vec2{first} * heightmap.spacing;
    const glm::vec2 max = glm::vec2{first + PatchQuads} * heightmap.spacing;
    return {.min = {min.x, low, min.y}, .max = {max.x, high, max.y}};
}

} // namespace levain::terrain
