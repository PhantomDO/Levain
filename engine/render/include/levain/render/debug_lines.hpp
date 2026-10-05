#pragma once

// Les lignes de debug (M6.2, ADR-0027) : des segments colorés dans le monde, dessinés par-dessus
// l'image avec le test de profondeur. Qui veut en dessiner remplit une liste à chaque image ; la
// passe la dessine en un seul appel.

#include <cstdint>
#include <span>

#include <glm/glm.hpp>
#include <nvrhi/nvrhi.h>

#include "levain/core/error.hpp"

namespace levain::render
{

/// Un segment, de `from` à `to`, d'une couleur. La couleur est en lumière linéaire, avant le
/// tonemapping, comme celle de tout ce qui est dessiné dans l'image HDR : (4, 4, 0) est un jaune
/// vif.
struct DebugLine
{
    glm::vec3 from{0.0f};
    glm::vec3 to{0.0f};
    glm::vec3 color{1.0f};
};

/// Les segments qu'une image peut dessiner : le buffer a cette taille, fixée à la création.
inline constexpr std::uint32_t MaxDebugLines = 65536;

struct DebugLinesPass
{
    nvrhi::ShaderHandle vertexShader;
    nvrhi::ShaderHandle pixelShader;
    nvrhi::InputLayoutHandle inputLayout;
    nvrhi::BindingLayoutHandle layout;
    nvrhi::GraphicsPipelineHandle pipeline;
    nvrhi::BufferHandle constants;
    nvrhi::BufferHandle vertices;
    nvrhi::BindingSetHandle bindings;
};

/// Crée la passe pour le framebuffer de la scène (couleur HDR et profondeur).
[[nodiscard]] core::Result<DebugLinesPass>
createDebugLinesPass(nvrhi::IDevice& device, const nvrhi::FramebufferInfo& target);

/// Dessine `lines` vus par `viewProjection`, avec le test de profondeur et sans écrire la
/// profondeur : une ligne cachée par un mur l'est aussi à l'écran. Au-delà de `MaxDebugLines`, les
/// lignes en trop ne sont pas dessinées, et le journal le dit une fois.
///
/// **Un appel par command list** : les sommets sont réécrits depuis le début à chaque appel. Sur
/// Vulkan, la copie est ordonnée dans la command list ; sur WebGPU, `queue.WriteBuffer` passe
/// avant toute la command list, et un second appel est refusé bruyamment par le backend. Qui veut
/// dessiner pour plusieurs systèmes réunit leurs lignes en une liste.
void drawDebugLines(nvrhi::ICommandList& commandList, const DebugLinesPass& pass,
                    nvrhi::IFramebuffer& target, const glm::mat4& viewProjection,
                    std::span<const DebugLine> lines);

} // namespace levain::render
