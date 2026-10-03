#pragma once

// Le rendu du lac (M5.7) : un carré d'eau plate, dessiné à l'étape `Transparent` (ADR-0025), après
// le ciel, et mêlé à ce qu'il recouvre. Le fond est le terrain : la profondeur d'eau se lit dans la
// heightmap du plugin terrain.

#include <nvrhi/nvrhi.h>

#include "levain/core/error.hpp"
#include "levain/render/culling.hpp"
#include "levain/render/frame.hpp"
#include "levain/render/stages.hpp"
#include "levain/terrain/heightmap.hpp"
#include "levain/terrain/terrain_pass.hpp"
#include "levain/water/lake.hpp"

namespace levain::water
{

struct WaterPass
{
    Lake lake;
    render::Box bounds; ///< Le carré du lac, plat, pour le frustum culling.
    nvrhi::TextureHandle ripples;
    nvrhi::SamplerHandle rippleSampler; ///< Répété et anisotrope : l'eau se voit de biais.
    /// La heightmap du terrain, et ce qui la ramène en mètres.
    nvrhi::TextureHandle heightmap;
    nvrhi::SamplerHandle heightSampler;
    float heightOffset = 0.0f;
    float heightScale = 1.0f;
    float spacing = 1.0f;
    float samples = 1.0f;
    nvrhi::ShaderHandle vertexShader;
    nvrhi::ShaderHandle pixelShader;
    nvrhi::BindingLayoutHandle layout; ///< space1, les ressources de l'eau.
    nvrhi::BufferHandle constants;
    nvrhi::BindingSetHandle bindings;
    nvrhi::GraphicsPipelineHandle pipeline;
};

/// Crée la passe du lac `lake`, posé sur le terrain de `terrainPass` et `heightmap`, éclairée par
/// `frame`, et enregistre l'envoi de sa carte de vaguelettes dans `commandList`.
[[nodiscard]] core::Result<WaterPass>
createWaterPass(nvrhi::IDevice& device, nvrhi::ICommandList& commandList, const Lake& lake,
                const terrain::TerrainPass& terrainPass, const terrain::Heightmap& heightmap,
                const render::FrameBindings& frame);

/// Dessine le lac, s'il touche le frustum : l'étape `Transparent`.
void drawWater(const render::StageContext& context, const WaterPass& pass);

/// Inscrit le dessin du lac à l'étape `Transparent`. `pass` doit vivre aussi longtemps que le
/// renderer : la fonction le garde par référence.
void addWaterPasses(render::RenderStages& stages, const WaterPass& pass);

} // namespace levain::water
