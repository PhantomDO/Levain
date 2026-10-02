#include "levain/render/light_clusters.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace levain::render
{

namespace
{

/// La direction, dans le repère de la caméra, du rayon qui passe par le point (`ndc`) de l'écran,
/// mise à l'échelle pour que z vaille −1 : un point à la profondeur d est `rayOf(ndc) * d`.
/// Toute profondeur de l'espace de découpe convient (0,5 ici) : en perspective, le rayon passe par
/// l'œil.
glm::vec3 rayOf(glm::vec2 ndc, const glm::mat4& inverseProjection)
{
    const glm::vec4 point = inverseProjection * glm::vec4{ndc, 0.5f, 1.0f};
    const glm::vec3 view = glm::vec3{point} / point.w;
    return view / -view.z;
}

} // namespace

std::uint32_t clusterCountOf(const ClusterGrid& grid)
{
    return grid.x * grid.y * grid.z;
}

std::uint32_t clusterIndexOf(const ClusterGrid& grid, glm::uvec3 cell)
{
    return cell.x + (grid.x * (cell.y + (grid.y * cell.z)));
}

float sliceDepthOf(const ClusterGrid& grid, const ClusterView& view, std::uint32_t slice)
{
    return view.nearPlane * std::pow(view.farPlane / view.nearPlane,
                                     static_cast<float>(slice) / static_cast<float>(grid.z));
}

ClusterBox clusterBoxOf(const ClusterGrid& grid, const ClusterView& view, glm::uvec3 cell)
{
    // Les quatre coins de la case à l'écran, en coordonnées normalisées (−1 à 1), puis leurs
    // rayons prolongés jusqu'aux deux profondeurs de la tranche.
    const glm::vec2 cells{static_cast<float>(grid.x), static_cast<float>(grid.y)};
    const glm::vec2 ndcMin = (glm::vec2{cell.x, cell.y} / cells * 2.0f) - 1.0f;
    const glm::vec2 ndcMax = (glm::vec2{cell.x + 1, cell.y + 1} / cells * 2.0f) - 1.0f;
    const glm::mat4 inverseProjection = glm::inverse(view.projection);
    const std::array<glm::vec2, 4> corners{ndcMin, glm::vec2{ndcMax.x, ndcMin.y},
                                           glm::vec2{ndcMin.x, ndcMax.y}, ndcMax};
    const std::array<float, 2> depths{sliceDepthOf(grid, view, cell.z),
                                      sliceDepthOf(grid, view, cell.z + 1)};
    ClusterBox box{.min = glm::vec3{std::numeric_limits<float>::max()},
                   .max = glm::vec3{std::numeric_limits<float>::lowest()}};
    for (const glm::vec2 corner : corners)
    {
        const glm::vec3 ray = rayOf(corner, inverseProjection);
        for (const float depth : depths)
        {
            box.min = glm::min(box.min, ray * depth);
            box.max = glm::max(box.max, ray * depth);
        }
    }
    return box;
}

bool sphereTouchesBox(glm::vec3 center, float radius, const ClusterBox& box)
{
    const glm::vec3 closest = glm::clamp(center, box.min, box.max);
    const glm::vec3 offset = center - closest;
    return glm::dot(offset, offset) <= radius * radius;
}

std::vector<std::vector<std::uint32_t>> lightsPerClusterOf(const ClusterGrid& grid,
                                                           const ClusterView& view,
                                                           std::span<const PointLight> lights)
{
    std::vector<std::vector<std::uint32_t>> result(clusterCountOf(grid));
    for (std::uint32_t z = 0; z < grid.z; ++z)
    {
        for (std::uint32_t y = 0; y < grid.y; ++y)
        {
            for (std::uint32_t x = 0; x < grid.x; ++x)
            {
                const glm::uvec3 cell{x, y, z};
                const ClusterBox box = clusterBoxOf(grid, view, cell);
                for (std::uint32_t light = 0; light < lights.size(); ++light)
                {
                    const glm::vec3 center =
                        glm::vec3{view.view * glm::vec4{lights[light].position, 1.0f}};
                    if (sphereTouchesBox(center, lights[light].range, box))
                    {
                        result[clusterIndexOf(grid, cell)].push_back(light);
                    }
                }
            }
        }
    }
    return result;
}

} // namespace levain::render
