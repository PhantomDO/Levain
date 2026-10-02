#pragma once

// La sortie HDR (M5.2) : la scène se dessine dans une texture en virgule flottante, où la lumière
// n'est pas coupée à 1, puis une passe plein écran la ramène dans l'image affichée.

#include <cstdint>

#include <nvrhi/nvrhi.h>

#include "levain/core/error.hpp"

namespace levain::render
{

/// Le format de l'image HDR : 16 bits flottants par canal, de quoi tenir le soleil comme l'ombre.
inline constexpr nvrhi::Format HdrFormat = nvrhi::Format::RGBA16_FLOAT;

/// Comment ramener la lumière de la scène dans l'image affichée.
struct TonemapSettings
{
    /// Multiplie la lumière avant tout : 2 éclaire d'un diaphragme, 0,5 assombrit d'un diaphragme.
    float exposure = 1.0f;
};

/// La passe plein écran : un triangle qui couvre l'image, et un shader qui lit l'image HDR.
struct TonemapPass
{
    nvrhi::ShaderHandle vertexShader;
    nvrhi::ShaderHandle pixelShader;
    nvrhi::BindingLayoutHandle layout;
    nvrhi::GraphicsPipelineHandle pipeline;
    nvrhi::BufferHandle constants;
    nvrhi::SamplerHandle sampler;
};

/// L'image HDR et son binding set, recréés ensemble quand la taille de l'image change.
struct HdrTarget
{
    nvrhi::TextureHandle texture;
    nvrhi::BindingSetHandle bindings;
};

/// Crée la passe pour des framebuffers de ce format (la swapchain), sans profondeur.
[[nodiscard]] core::Result<TonemapPass> createTonemapPass(nvrhi::IDevice& device,
                                                          const nvrhi::FramebufferInfo& output);

/// L'image HDR à cette taille : recréée seulement quand elle change.
[[nodiscard]] nvrhi::ITexture* ensureHdrTarget(nvrhi::IDevice& device, const TonemapPass& pass,
                                               HdrTarget& target, std::uint32_t width,
                                               std::uint32_t height);

/// Enregistre la passe : l'image HDR de `target`, exposée, dessinée dans `output`.
void tonemap(nvrhi::ICommandList& commandList, const TonemapPass& pass, const HdrTarget& target,
             nvrhi::IFramebuffer& output, const TonemapSettings& settings);

} // namespace levain::render
