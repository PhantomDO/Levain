#pragma once

// Le renderer (ADR-0025) : il possède l'image et l'ordre de ses passes. Le tri des lumières en
// clusters, l'éclairage de la frame, les ombres, les surfaces opaques, le ciel, ce qui se mélange,
// puis le tonemapping. Ce qui se dessine, il ne le connaît pas : ce sont les fonctions inscrites
// dans ses étapes (`stages`), celles de l'application comme celles des plugins.

#include <array>
#include <optional>
#include <span>
#include <string_view>

#include <glm/glm.hpp>
#include <nvrhi/nvrhi.h>

#include "levain/core/error.hpp"
#include "levain/render/camera.hpp"
#include "levain/render/environment.hpp"
#include "levain/render/frame.hpp"
#include "levain/render/gpu_timer.hpp"
#include "levain/render/light_clusters.hpp"
#include "levain/render/mesh_pass.hpp"
#include "levain/render/shadows.hpp"
#include "levain/render/sky.hpp"
#include "levain/render/stages.hpp"
#include "levain/render/tonemap.hpp"

namespace levain::render
{

/// Les passes de l'image chronométrées sur le GPU (#133), dans l'ordre où elles passent. Une étape
/// où rien n'est inscrit n'est pas chronométrée.
inline constexpr std::array<std::string_view, 6> RendererPassNames{
    "clusters", "ombres", "opaques", "ciel", "transparents", "tonemapping"};

struct Renderer
{
    LightClusterPass clusters;
    CascadeSettings cascadeSettings;
    ShadowPass shadows;
    Environment environment;
    FrameBindings frame;
    /// Pour dessiner les meshes dans l'étape `Opaque` : la passe ne dessine rien d'elle-même.
    MeshPass meshPass;
    std::optional<SkyPass> sky; ///< Sans lui, le fond reste la couleur d'effacement.
    TonemapPass tonemap;
    HdrTarget hdr;              ///< Créée à la première image, à sa taille.
    nvrhi::TextureHandle depth; ///< Idem.
    RenderStages stages;
    std::array<GpuTimer, RendererPassNames.size()> timers;
    std::array<GpuTimeAverage, RendererPassNames.size()> passTimes;
};

/// Les formats où dessine le renderer : l'image HDR et sa profondeur. Ceux des pipelines qu'une
/// fonction d'étape crée pour `Opaque` et `Transparent`.
[[nodiscard]] nvrhi::FramebufferInfo sceneTargetInfo();

/// Crée les passes de l'image, pour une sortie (la swapchain) au format `outputFormat`, éclairée
/// par `environment`, avec le ciel en fond si `drawSky`.
[[nodiscard]] core::Result<Renderer> createRenderer(nvrhi::IDevice& device,
                                                    nvrhi::Format outputFormat,
                                                    Environment environment, bool drawSky);

/// Ce que l'image montre : la vue, ses lumières, et comment la ramener à l'écran.
struct FrameView
{
    Camera camera;
    Sun sun;
    float environmentIntensity = 1.0f;
    std::span<const PointLight> lights;
    TonemapSettings tonemap;
    glm::vec4 background{0.0f, 0.0f, 0.0f, 1.0f}; ///< Là où ni dessin ni ciel ne passent.
    double seconds = 0.0;
};

/// Enregistre l'image dans `commandList`, ouverte par l'appelant, et la ramène dans `output`, à sa
/// taille.
void renderFrame(nvrhi::IDevice& device, nvrhi::ICommandList& commandList, Renderer& renderer,
                 const FrameView& view, nvrhi::ITexture& output);

} // namespace levain::render
