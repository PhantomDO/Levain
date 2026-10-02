#pragma once

// Le forward+ en clusters (ADR-0024) : le volume de la caméra découpé en une grille 3D, et pour
// chaque case (un cluster), la liste des lumières ponctuelles qui la touchent. Un compute la
// remplit à chaque image ; le shader d'éclairage n'y lit que les lumières de son cluster.
//
// Les fonctions CPU de ce fichier font le même calcul que shaders/light_clusters.slang : elles
// servent de référence aux tests (tests/light_clusters_test.cpp, tests/light_clusters_gpu.cpp).

#include <cstdint>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <nvrhi/nvrhi.h>

#include "levain/core/error.hpp"

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

static_assert(sizeof(PointLight) == 32, "disposition lue par shaders/light_clusters.slang");

/// La grille : `x` × `y` cases à l'écran, `z` tranches en profondeur, de plus en plus épaisses avec
/// la distance (réparties de façon logarithmique entre le plan proche et le plan lointain).
struct ClusterGrid
{
    std::uint32_t x = 16;
    std::uint32_t y = 9;
    std::uint32_t z = 24;
};

/// Au-delà, `assignLightsToClusters` refuse la scène : une limite bruyante plutôt qu'une lumière
/// qui disparaîtrait en silence (ADR-0024).
inline constexpr std::uint32_t MaxPointLights = 256;
/// Les lumières qu'un cluster retient. Le compte, lui, reste exact : un cluster plus chargé se voit
/// dans `lightCounts`.
inline constexpr std::uint32_t MaxLightsPerCluster = 32;

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

/// La passe de tri et ses buffers. `lightCounts` : un compte par cluster ; `lightIndices` :
/// `MaxLightsPerCluster` places par cluster, dont les `min(compte, MaxLightsPerCluster)` premières
/// sont remplies.
struct LightClusterPass
{
    ClusterGrid grid;
    nvrhi::ShaderHandle shader;
    nvrhi::BindingLayoutHandle layout;
    nvrhi::ComputePipelineHandle pipeline;
    nvrhi::BufferHandle constants;
    nvrhi::BufferHandle lights;
    nvrhi::BufferHandle lightCounts;
    nvrhi::BufferHandle lightIndices;
    nvrhi::BindingSetHandle bindings;
};

[[nodiscard]] core::Result<LightClusterPass> createLightClusterPass(nvrhi::IDevice& device,
                                                                    const ClusterGrid& grid = {});

/// Enregistre l'envoi des lumières et le tri, dans `commandList`, avant les dessins qui les lisent
/// : NVRHI place la barrière entre l'écriture du compute et la lecture des shaders. Échoue au-delà
/// de `MaxPointLights` lumières.
[[nodiscard]] core::Result<void> assignLightsToClusters(nvrhi::ICommandList& commandList,
                                                        const LightClusterPass& pass,
                                                        std::span<const PointLight> lights,
                                                        const ClusterView& view);

} // namespace levain::render
