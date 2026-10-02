#include "levain/render/shadows.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace levain::render
{

namespace
{

/// Ce que la projection du soleil regarde en plus, entre lui et la tranche : un objet hors de la
/// tranche peut y jeter son ombre (un arbre derrière la caméra, une falaise).
// ponytail: une marge fixe de 50 m ; l'ajuster à la scène (ses objets vus du soleil) quand un
// relief plus haut que ça perdra son ombre.
constexpr float CasterMargin = 50.0f;

/// Le rayon arrondi au 1/16 supérieur : le flottant du calcul varie d'une image à l'autre, et un
/// rayon qui bouge d'un rien changerait la taille des texels.
float roundedRadius(float radius)
{
    return std::ceil(radius * 16.0f) / 16.0f;
}

} // namespace

std::array<float, CascadeCount + 1> cascadeSplitsOf(const Camera& camera,
                                                    const CascadeSettings& settings)
{
    const float nearDepth = camera.nearPlane;
    const float farDepth = std::min(settings.shadowDistance, camera.farPlane);
    std::array<float, CascadeCount + 1> splits{};
    for (std::uint32_t i = 0; i <= CascadeCount; ++i)
    {
        const float ratio = static_cast<float>(i) / static_cast<float>(CascadeCount);
        const float uniform = nearDepth + ((farDepth - nearDepth) * ratio);
        const float logarithmic = nearDepth * std::pow(farDepth / nearDepth, ratio);
        splits[i] = glm::mix(uniform, logarithmic, settings.splitBlend);
    }
    // Exactement les deux bouts, sans l'écart d'arrondi du calcul.
    splits.front() = nearDepth;
    splits.back() = farDepth;
    return splits;
}

std::array<glm::vec3, 8> frustumSliceCornersOf(const Camera& camera, float aspectRatio,
                                               float nearDepth, float farDepth)
{
    const glm::vec3 forward = glm::normalize(camera.target - camera.position);
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3{0.0f, 1.0f, 0.0f}));
    const glm::vec3 up = glm::cross(right, forward);
    const float tanHalfFov = std::tan(camera.verticalFovRadians * 0.5f);
    std::array<glm::vec3, 8> corners{};
    std::size_t corner = 0;
    for (const float depth : {nearDepth, farDepth})
    {
        const glm::vec3 center = camera.position + (forward * depth);
        const float halfHeight = depth * tanHalfFov;
        const float halfWidth = halfHeight * aspectRatio;
        for (const float x : {-1.0f, 1.0f})
        {
            for (const float y : {-1.0f, 1.0f})
            {
                corners[corner++] = center + (right * (x * halfWidth)) + (up * (y * halfHeight));
            }
        }
    }
    return corners;
}

Cascade cascadeOf(const std::array<glm::vec3, 8>& sliceCorners, float farDepth,
                  glm::vec3 sunDirection, std::uint32_t resolution)
{
    glm::vec3 center{0.0f};
    for (const glm::vec3& corner : sliceCorners)
    {
        center += corner / static_cast<float>(sliceCorners.size());
    }
    float radius = 0.0f;
    for (const glm::vec3& corner : sliceCorners)
    {
        radius = std::max(radius, glm::length(corner - center));
    }
    radius = roundedRadius(radius);

    // Le soleil regarde le centre de la sphère, depuis assez loin pour voir la marge des objets qui
    // projettent leur ombre dans la tranche.
    const glm::vec3 toSun = glm::normalize(sunDirection);
    const glm::vec3 up =
        std::abs(toSun.y) > 0.99f ? glm::vec3{0.0f, 0.0f, 1.0f} : glm::vec3{0.0f, 1.0f, 0.0f};
    const glm::mat4 view = glm::lookAtRH(center + (toSun * (radius + CasterMargin)), center, up);
    glm::mat4 projection =
        glm::orthoRH_ZO(-radius, radius, -radius, radius, 0.0f, (2.0f * radius) + CasterMargin);

    // L'origine du monde, projetée, doit tomber sur un texel entier : la projection n'avance alors
    // que par texels, et les bords des ombres ne glissent pas d'une image à l'autre.
    const glm::vec4 origin = projection * view * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
    const float texelsPerUnit = static_cast<float>(resolution) * 0.5f;
    const glm::vec2 inTexels = glm::vec2{origin} * texelsPerUnit;
    const glm::vec2 offset = (glm::round(inTexels) - inTexels) / texelsPerUnit;
    projection[3][0] += offset.x;
    projection[3][1] += offset.y;

    return Cascade{.viewProjection = projection * view, .farDepth = farDepth};
}

std::array<Cascade, CascadeCount> cascadesOf(const Camera& camera, float aspectRatio,
                                             glm::vec3 sunDirection,
                                             const CascadeSettings& settings)
{
    const std::array<float, CascadeCount + 1> splits = cascadeSplitsOf(camera, settings);
    std::array<Cascade, CascadeCount> cascades{};
    for (std::uint32_t i = 0; i < CascadeCount; ++i)
    {
        cascades[i] =
            cascadeOf(frustumSliceCornersOf(camera, aspectRatio, splits[i], splits[i + 1]),
                      splits[i + 1], sunDirection, settings.resolution);
    }
    return cascades;
}

} // namespace levain::render
