#pragma once

// Les lignes de debug (M6.2, ADR-0027) : des segments colorés dans le monde, dessinés par-dessus
// l'image avec le test de profondeur. Qui veut en dessiner remplit une liste à chaque image ; la
// passe la dessine en un seul appel.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include <glm/glm.hpp>
#include <nvrhi/nvrhi.h>

#include "levain/core/error.hpp"
#include "levain/render/culling.hpp"

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
    /// L'opacité à chaque bout, pour `DebugBlend::Alpha` (ignorée en `Opaque`) : le rasterizer
    /// l'interpole le long du segment, de quoi estomper une ligne avec la distance.
    float alphaFrom = 1.0f;
    float alphaTo = 1.0f;
};

/// **Le segment de `line` qui est dans le volume de vue**, ou rien s'il est tout dehors. Les
/// sommets sont coupés contre les six plans du frustum avant le dessin, couleur et opacité
/// interpolées. Sans cela, une ligne qui part loin devant la caméra et finit loin derrière elle
/// (la grille du sol) a son bout derrière l'œil projeté à des dizaines de milliers de pixels hors
/// écran, ou rejeté, selon que le backend écrête ou non la profondeur proche (Vulkan : non,
/// Direct3D 12 : `DepthClipEnable` suit NVRHI, build/GOTCHA.md) ; les lignes qui traversent le plan
/// proche disparaissent alors, ou s'affichent en désordre.
[[nodiscard]] std::optional<DebugLine> clipToFrustum(const DebugLine& line, const Frustum& frustum);

/// Comment les lignes se mêlent à ce qui est dessiné dessous.
enum class DebugBlend : std::uint8_t
{
    /// Écrasent ce qui est dessous : l'opacité des lignes est ignorée.
    Opaque,
    /// Mêlées par leur opacité (`alphaFrom`, `alphaTo`) : la grille s'estompe sans devenir noire.
    Alpha,
};

/// Comment les lignes se mêlent à la scène.
enum class DebugDepth : std::uint8_t
{
    /// Testées contre la profondeur : une ligne cachée par un mur l'est aussi à l'écran.
    Tested,
    /// Par-dessus ce qui est déjà dessiné : le contour d'une sélection, dont les arêtes sont sur
    /// les faces mêmes de l'objet, et que le test de profondeur rejetterait à égalité. Ce qui est
    /// dessiné ensuite (une étape plus tardive, un plugin inscrit après) le couvre encore.
    OnTop,
};

/// Les segments qu'une image peut dessiner : le buffer a cette taille, fixée à la création.
inline constexpr std::uint32_t MaxDebugLines = 65536;

struct DebugLinesPass
{
    nvrhi::ShaderHandle vertexShader;
    nvrhi::ShaderHandle pixelShader;
    nvrhi::InputLayoutHandle inputLayout;
    nvrhi::BindingLayoutHandle layout;
    nvrhi::GraphicsPipelineHandle pipeline;             ///< `DebugDepth::Tested`.
    nvrhi::GraphicsPipelineHandle onTopPipeline;        ///< `DebugDepth::OnTop`.
    nvrhi::GraphicsPipelineHandle blendedPipeline;      ///< `Tested` + `DebugBlend::Alpha`.
    nvrhi::GraphicsPipelineHandle blendedOnTopPipeline; ///< `OnTop` + `DebugBlend::Alpha`.
    nvrhi::BufferHandle constants;
    nvrhi::BufferHandle vertices;
    nvrhi::BindingSetHandle bindings;
};

/// Crée la passe pour le framebuffer de la scène (couleur HDR et profondeur).
[[nodiscard]] core::Result<DebugLinesPass>
createDebugLinesPass(nvrhi::IDevice& device, const nvrhi::FramebufferInfo& target);

/// Dessine `lines` vus par `viewProjection`, sans écrire la profondeur : une ligne ne cache rien.
/// Chaque ligne est d'abord coupée au volume de vue (`clipToFrustum`) : ce qui est tout dehors
/// n'est pas envoyé, et le plafond de `MaxDebugLines` compte les lignes qui restent. Au-delà de
/// `MaxDebugLines`, les lignes en trop ne sont pas dessinées, et le journal le dit une fois. **Rend
/// le nombre de lignes envoyées** (après la coupe et le plafond) : qui compte ce qu'il a dessiné
/// lit ce nombre, non la taille de sa liste, que la coupe peut vider (une grille toute dehors).
///
/// **Un appel par command list** : les sommets sont réécrits depuis le début à chaque appel. Sur
/// Vulkan, la copie est ordonnée dans la command list ; sur WebGPU, `queue.WriteBuffer` passe
/// avant toute la command list, et un second appel est refusé bruyamment par le backend. Qui veut
/// dessiner pour plusieurs systèmes réunit leurs lignes en une liste.
std::size_t drawDebugLines(nvrhi::ICommandList& commandList, const DebugLinesPass& pass,
                           nvrhi::IFramebuffer& target, const glm::mat4& viewProjection,
                           std::span<const DebugLine> lines, DebugDepth depth = DebugDepth::Tested,
                           DebugBlend blend = DebugBlend::Opaque);

} // namespace levain::render
