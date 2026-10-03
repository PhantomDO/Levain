#include "levain/render/culling.hpp"

#include <limits>

namespace levain::render
{

Box boundsOf(std::span<const glm::vec3> points)
{
    if (points.empty())
    {
        return {};
    }
    Box box{.min = glm::vec3{std::numeric_limits<float>::max()},
            .max = glm::vec3{std::numeric_limits<float>::lowest()}};
    for (const glm::vec3& point : points)
    {
        box.min = glm::min(box.min, point);
        box.max = glm::max(box.max, point);
    }
    return box;
}

Box transformed(const Box& box, const glm::mat4& transform)
{
    // Partir de la translation, puis ajouter, axe par axe, le plus petit et le plus grand des deux
    // produits : un coefficient négatif inverse le rôle de min et de max.
    Box result{.min = glm::vec3{transform[3]}, .max = glm::vec3{transform[3]}};
    for (int column = 0; column < 3; ++column)
    {
        const glm::vec3 axis{transform[column]};
        const glm::vec3 low = axis * box.min[column];
        const glm::vec3 high = axis * box.max[column];
        result.min += glm::min(low, high);
        result.max += glm::max(low, high);
    }
    return result;
}

Frustum frustumOf(const glm::mat4& viewProjection)
{
    // GLM range ses matrices par colonnes : la ligne i se lit en travers des quatre colonnes.
    const auto row = [&viewProjection](int i)
    {
        return glm::vec4{viewProjection[0][i], viewProjection[1][i], viewProjection[2][i],
                         viewProjection[3][i]};
    };
    Frustum frustum{.planes = {
                        row(3) + row(0), // gauche :  -w ≤ x
                        row(3) - row(0), // droite :   x ≤ w
                        row(3) + row(1), // bas :     -w ≤ y
                        row(3) - row(1), // haut :     y ≤ w
                        row(2),          // proche :   0 ≤ z (et non -w ≤ z, comme sous OpenGL)
                        row(3) - row(2), // lointain : z ≤ w
                    }};
    for (glm::vec4& plane : frustum.planes)
    {
        plane /= glm::length(glm::vec3{plane});
    }
    return frustum;
}

bool isOutside(const Frustum& frustum, const Box& box)
{
    for (const glm::vec4& plane : frustum.planes)
    {
        const glm::vec3 normal{plane};
        const glm::vec3 farthest{normal.x >= 0.0f ? box.max.x : box.min.x,
                                 normal.y >= 0.0f ? box.max.y : box.min.y,
                                 normal.z >= 0.0f ? box.max.z : box.min.z};
        if (glm::dot(normal, farthest) + plane.w < 0.0f)
        {
            return true;
        }
    }
    return false;
}

} // namespace levain::render
