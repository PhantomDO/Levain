#pragma once

// Le rendu de l'herbe (M5.7) : une forme de brin, instanciée des milliers de fois par parcelle du
// terrain, à l'étape `Opaque` (ADR-0025). Les brins se posent sur la heightmap du terrain, et se
// gardent ou s'écartent selon la carte de densité.

#include <cstdint>
#include <vector>

#include <nvrhi/nvrhi.h>

#include "levain/core/error.hpp"
#include "levain/grass/grass.hpp"
#include "levain/render/culling.hpp"
#include "levain/render/frame.hpp"
#include "levain/render/stages.hpp"
#include "levain/terrain/heightmap.hpp"
#include "levain/terrain/terrain_pass.hpp"

namespace levain::grass
{

struct GrassPass
{
    GrassSettings settings;
    nvrhi::BufferHandle vertices;
    nvrhi::BufferHandle indices;
    std::uint32_t indexCount = 0;
    nvrhi::TextureHandle density;
    /// La heightmap du terrain, et ce qui la ramène en mètres.
    nvrhi::TextureHandle heightmap;
    nvrhi::SamplerHandle sampler;
    float heightOffset = 0.0f;
    float heightScale = 1.0f;
    float spacing = 1.0f;
    float samples = 1.0f;
    float patchSize = 0.0f;
    std::uint32_t patchesPerSide = 0;
    std::vector<render::Box>
        patchBounds; ///< Celles du terrain, rehaussées de la hauteur des brins.
    nvrhi::ShaderHandle vertexShader;
    nvrhi::ShaderHandle pixelShader;
    nvrhi::InputLayoutHandle inputLayout;
    nvrhi::BindingLayoutHandle layout; ///< space1, les ressources de l'herbe.
    nvrhi::BufferHandle constants;
    nvrhi::BindingSetHandle bindings;
    nvrhi::GraphicsPipelineHandle pipeline;
};

/// Crée la passe de l'herbe sur le terrain de `terrainPass` et `heightmap`, hors de l'eau sous
/// `waterLevel`, éclairée par `frame`, et enregistre l'envoi de ses buffers et de sa carte dans
/// `commandList`.
[[nodiscard]] core::Result<GrassPass>
createGrassPass(nvrhi::IDevice& device, nvrhi::ICommandList& commandList,
                const terrain::TerrainPass& terrainPass, const terrain::Heightmap& heightmap,
                float waterLevel, const render::FrameBindings& frame,
                const GrassSettings& settings = {});

/// Ce que les dessins de l'herbe ont soumis depuis le début.
struct GrassStats
{
    std::uint64_t patches = 0; ///< Les parcelles dessinées.
    std::uint64_t blades = 0;  ///< Les brins demandés, avant que la densité n'en écarte.
};

/// Dessine les brins des parcelles proches de la caméra et dans son frustum : l'étape `Opaque`.
void drawGrass(const render::StageContext& context, const GrassPass& pass, GrassStats& stats);

/// Inscrit le dessin de l'herbe à l'étape `Opaque`. `pass` et `stats` doivent vivre aussi
/// longtemps que le renderer : la fonction les garde par référence.
void addGrassPasses(render::RenderStages& stages, const GrassPass& pass, GrassStats& stats);

} // namespace levain::grass
