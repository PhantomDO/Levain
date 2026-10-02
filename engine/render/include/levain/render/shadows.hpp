#pragma once

// Les ombres du soleil en cascades (M5.3). Une seule shadow map couvrirait mal un monde vu de près
// comme de loin : ses texels seraient trop gros devant la caméra. On découpe donc le volume de vue
// en tranches de profondeur, et chaque tranche a sa propre shadow map (une cascade), d'autant plus
// grande que la tranche est loin. Ce fichier calcule les tranches et les projections du soleil, et
// dessine la profondeur vue du soleil dans un atlas : les quatre cascades en 2 × 2.

#include <array>
#include <cstdint>

#include <glm/glm.hpp>
#include <nvrhi/nvrhi.h>

#include "levain/core/error.hpp"
#include "levain/render/camera.hpp"
#include "levain/render/mesh.hpp"

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

/// Le format de l'atlas des ombres : la profondeur seule, 32 bits flottants.
inline constexpr nvrhi::Format ShadowFormat = nvrhi::Format::D32;

/// La passe d'ombres : son pipeline, sans fragment shader, et l'atlas où elle dessine, une cascade
/// par quart (`atlasCellOf`). Un atlas plutôt qu'un tableau de textures : une seule texture 2D, la
/// même sur Vulkan et sur WebGPU.
struct ShadowPass
{
    std::uint32_t resolution = 0; ///< Les texels d'une cascade sur chaque côté.
    nvrhi::ShaderHandle vertexShader;
    nvrhi::InputLayoutHandle inputLayout;
    nvrhi::BindingLayoutHandle layout;
    nvrhi::GraphicsPipelineHandle pipeline;
    nvrhi::BufferHandle constants;
    nvrhi::BindingSetHandle bindings;
    nvrhi::TextureHandle atlas;
    nvrhi::FramebufferHandle framebuffer;
    /// Pour lire l'atlas : il compare la profondeur au lieu de la rendre, et le GPU filtre le
    /// résultat sur les 2 × 2 texels voisins.
    nvrhi::SamplerHandle sampler;
};

[[nodiscard]] core::Result<ShadowPass> createShadowPass(nvrhi::IDevice& device,
                                                        std::uint32_t resolution);

/// Le coin de la cascade `cascade` dans l'atlas, en cascades : (0, 0), (1, 0), (0, 1), (1, 1).
[[nodiscard]] glm::uvec2 atlasCellOf(std::uint32_t cascade);

/// Efface l'atlas, au plus loin : à enregistrer avant les dessins de l'image.
void clearShadows(nvrhi::ICommandList& commandList, const ShadowPass& pass);

/// Dessine les `instances` de `mesh`, placées par `model`, dans la cascade `cascade`.
void drawShadowCaster(nvrhi::ICommandList& commandList, const ShadowPass& pass,
                      std::uint32_t cascade, const Cascade& view, const Mesh& mesh,
                      const Instances& instances, const glm::mat4& model);

} // namespace levain::render
