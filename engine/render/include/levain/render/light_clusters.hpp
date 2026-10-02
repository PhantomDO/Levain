#pragma once

// Le forward+ en clusters (ADR-0024) : le volume de la caméra découpé en une grille 3D, et pour
// chaque case (un cluster), la liste des lumières ponctuelles qui la touchent. Un compute la
// remplit à chaque image ; le shader d'éclairage n'y lit que les lumières de son cluster.
//
// Ce fichier donne le découpage et la référence CPU du tri ; la passe compute, qui fera le même
// calcul sur le GPU, viendra ensuite et sera comparée à cette référence.

#include <cstdint>
#include <span>
#include <vector>

#include <glm/glm.hpp>

namespace levain::render
{

/// Une lumière ponctuelle, dans le repère du monde, telle que la lisent les shaders.
struct PointLight
{
    glm::vec3 position{0.0f};
    float range = 1.0f; ///< Au-delà, la lumière n'éclaire plus rien.
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
};

static_assert(sizeof(PointLight) == 32, "disposition que liront les shaders");

/// La grille : `x` × `y` cases à l'écran, `z` tranches en profondeur, de plus en plus épaisses avec
/// la distance (réparties de façon logarithmique entre le plan proche et le plan lointain).
struct ClusterGrid
{
    std::uint32_t x = 16;
    std::uint32_t y = 9;
    std::uint32_t z = 24;
};

/// Ce que le découpage doit savoir de la caméra.
struct ClusterView
{
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
};

/// Une boîte alignée sur les axes, dans le repère de la caméra (qui regarde vers −Z).
struct ClusterBox
{
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

[[nodiscard]] std::uint32_t clusterCountOf(const ClusterGrid& grid);

/// L'indice d'un cluster : `x` varie le plus vite, puis `y`, puis `z`.
[[nodiscard]] std::uint32_t clusterIndexOf(const ClusterGrid& grid, glm::uvec3 cell);

/// La profondeur (distance devant la caméra) où commence la tranche `slice` ; la tranche `z` est le
/// plan lointain.
[[nodiscard]] float sliceDepthOf(const ClusterGrid& grid, const ClusterView& view,
                                 std::uint32_t slice);

/// La boîte qui contient le cluster `cell`.
[[nodiscard]] ClusterBox clusterBoxOf(const ClusterGrid& grid, const ClusterView& view,
                                      glm::uvec3 cell);

/// Vrai si la sphère touche la boîte : la distance du centre au point le plus proche de la boîte ne
/// dépasse pas le rayon.
[[nodiscard]] bool sphereTouchesBox(glm::vec3 center, float radius, const ClusterBox& box);

/// La référence CPU : pour chaque cluster, les indices de toutes les lumières qui le touchent, dans
/// l'ordre de `lights`, sans limite.
[[nodiscard]] std::vector<std::vector<std::uint32_t>>
lightsPerClusterOf(const ClusterGrid& grid, const ClusterView& view,
                   std::span<const PointLight> lights);

} // namespace levain::render
