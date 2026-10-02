#pragma once

// Les ombres du soleil en cascades (M5.3). Une seule shadow map couvrirait mal un monde vu de près
// comme de loin : ses texels seraient trop gros devant la caméra. On découpe donc le volume de vue
// en tranches de profondeur, et chaque tranche a sa propre shadow map (une cascade), d'autant plus
// grande que la tranche est loin. Ce fichier calcule les tranches et les projections du soleil ;
// la passe qui dessine les ombres viendra ensuite.

#include <array>
#include <cstdint>

#include <glm/glm.hpp>

#include "levain/render/camera.hpp"

namespace levain::render
{

/// Le nombre de cascades.
inline constexpr std::uint32_t CascadeCount = 4;

struct CascadeSettings
{
    /// Au-delà, plus d'ombre : les cascades se partagent la profondeur de 0 à cette distance.
    float shadowDistance = 100.0f;
    /// Le partage des tranches : 0 en tranches égales, 1 en tranches de plus en plus épaisses, au
    /// même rapport (logarithmiques). Entre les deux, le mélange « pratique » de Zhang et al.
    float splitBlend = 0.75f;
    /// Les texels d'une cascade sur chaque côté.
    std::uint32_t resolution = 2048;
};

/// Une cascade : ce que voit le soleil pour ombrer une tranche de la vue.
struct Cascade
{
    glm::mat4 viewProjection{1.0f}; ///< Du monde vers l'espace de découpe de la shadow map.
    float farDepth = 0.0f;          ///< La profondeur, devant la caméra, où finit la tranche.
};

/// Les profondeurs qui séparent les cascades : `CascadeCount + 1` valeurs, du plan proche de la
/// caméra à `shadowDistance`.
[[nodiscard]] std::array<float, CascadeCount + 1> cascadeSplitsOf(const Camera& camera,
                                                                  const CascadeSettings& settings);

/// Les huit coins de la tranche du volume de vue entre `nearDepth` et `farDepth`, dans le repère
/// du monde.
[[nodiscard]] std::array<glm::vec3, 8>
frustumSliceCornersOf(const Camera& camera, float aspectRatio, float nearDepth, float farDepth);

/// La cascade d'une tranche, vue du soleil (`sunDirection` : vers le soleil). La tranche est
/// enfermée dans une sphère, dont la taille ne change pas quand la caméra tourne, et la projection
/// avance par texels entiers : les bords des ombres ne scintillent pas quand la caméra bouge.
[[nodiscard]] Cascade cascadeOf(const std::array<glm::vec3, 8>& sliceCorners, float farDepth,
                                glm::vec3 sunDirection, std::uint32_t resolution);

/// Les cascades de la vue.
[[nodiscard]] std::array<Cascade, CascadeCount> cascadesOf(const Camera& camera, float aspectRatio,
                                                           glm::vec3 sunDirection,
                                                           const CascadeSettings& settings);

} // namespace levain::render
