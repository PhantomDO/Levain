#include "levain/editor/grid.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace levain::editor
{

namespace
{

/// **Le centre est calé sur le pas** : il est à au plus un demi-pas de la caméra sur chaque axe,
/// soit `√2 / 2` pas, 0,71 en arrondissant par excès. La portée en tient compte (`gridReachFor`).
constexpr float GridCenterSnap = 0.71f;
/// L'indice du plan lointain parmi les six de `render::frustumOf` : gauche, droite, bas, haut,
/// proche, **lointain**.
constexpr std::size_t FarPlaneIndex = 5;

} // namespace

float gridReachFor(float step, float cameraHeight, float farPlane)
{
    // Un plan lointain négatif mis au carré redeviendrait positif : on l'écarte avant. `!(x > 0)`
    // et non `x <= 0` : un plan lointain NaN donne une portée nulle lui aussi.
    if (!(farPlane > 0.0f))
    {
        return 0.0f;
    }
    const float visible = GridFarFraction * farPlane;
    const float onGround =
        std::sqrt(std::max(visible * visible - cameraHeight * cameraHeight, 0.0f));
    const float reach = onGround - GridCenterSnap * step;
    return !(reach > 0.0f) ? 0.0f : std::min(reach, static_cast<float>(GridHalfCells) * step);
}

float farDistanceOf(const render::Frustum& frustum, const glm::vec3& cameraPosition)
{
    const glm::vec4& plane = frustum.planes.at(FarPlaneIndex);
    return glm::dot(glm::vec3{plane}, cameraPosition) + plane.w;
}

std::array<GridLevel, GridLevelCount> gridLevelsFor(float cameraHeight)
{
    const float height = std::clamp(std::abs(cameraHeight), 0.1f, 100000.0f);
    const float exponent = std::log10(height);
    const float decade = std::floor(exponent);
    // La partie fractionnaire du logarithme : 0 à une puissance de 10, 1 juste avant la suivante.
    const float blend = exponent - decade;
    const float step = std::pow(10.0f, decade);
    const float tenth = static_cast<float>(GridMajorEvery);
    return {{{.step = step / tenth, .role = 0.0f, .visibility = 1.0f - blend},
             {.step = step, .role = 1.0f - blend, .visibility = 1.0f},
             {.step = step * tenth, .role = 1.0f, .visibility = blend}}};
}

GridSpacing gridSpacingFor(float cameraHeight)
{
    const std::array<GridLevel, GridLevelCount> levels = gridLevelsFor(cameraHeight);
    return {.minor = levels[1].step, .major = levels[2].step};
}

} // namespace levain::editor
