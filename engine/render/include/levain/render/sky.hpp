#pragma once

// Le ciel en fond (M5.4) : la cubemap de l'environnement, vue de la caméra, là où aucun mesh n'est
// dessiné.

#include <nvrhi/nvrhi.h>

#include "levain/core/error.hpp"
#include "levain/render/camera.hpp"
#include "levain/render/environment.hpp"

namespace levain::render
{

struct SkyPass
{
    nvrhi::ShaderHandle vertexShader;
    nvrhi::ShaderHandle pixelShader;
    nvrhi::BindingLayoutHandle layout;
    nvrhi::GraphicsPipelineHandle pipeline;
    nvrhi::BufferHandle constants;
    nvrhi::BindingSetHandle bindings;
};

/// Crée la passe pour le framebuffer de la scène (couleur HDR et `DepthFormat`), qui lit le ciel de
/// `environment`.
[[nodiscard]] core::Result<SkyPass> createSkyPass(nvrhi::IDevice& device,
                                                  const nvrhi::FramebufferInfo& target,
                                                  const Environment& environment);

/// Dessine le ciel vu de `camera`, multiplié par `intensity` (celle de l'éclairage par l'image,
/// `FrameLighting::environmentIntensity`), après les meshes : seuls les pixels encore au plan
/// lointain le reçoivent.
void drawSky(nvrhi::ICommandList& commandList, const SkyPass& pass, nvrhi::IFramebuffer& target,
             const Camera& camera, float aspectRatio, float intensity);

} // namespace levain::render
