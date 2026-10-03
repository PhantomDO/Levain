#pragma once

// Le rendu du terrain (M5.6) : la heightmap et la carte de poids en textures, une grille de sommets
// par niveau de détail, et deux pipelines, l'un éclairé pour l'étape `Opaque`, l'autre pour l'ombre
// portée dans chaque cascade (`ShadowCasters`). Le renderer appelle ses fonctions, inscrites par
// `addTerrainPasses` (ADR-0025).

#include <array>
#include <cstdint>
#include <vector>

#include <nvrhi/nvrhi.h>

#include "levain/core/error.hpp"
#include "levain/render/culling.hpp"
#include "levain/render/frame.hpp"
#include "levain/render/renderer.hpp"
#include "levain/render/shadows.hpp"
#include "levain/render/stages.hpp"
#include "levain/terrain/heightmap.hpp"
#include "levain/terrain/patches.hpp"

namespace levain::terrain
{

/// La grille d'une parcelle à un niveau de détail : des sommets de 0 à 1 sur ses deux côtés.
struct PatchGrid
{
    nvrhi::BufferHandle vertices;
    nvrhi::BufferHandle indices;
    std::uint32_t indexCount = 0;
};

struct TerrainPass
{
    nvrhi::TextureHandle
        heightmap; ///< R16 flottant, de 0 à 1 : de `heightOffset` à + `heightScale`.
    nvrhi::TextureHandle weights;
    nvrhi::SamplerHandle sampler;
    float heightOffset = 0.0f;
    float heightScale = 1.0f;
    std::array<PatchGrid, MaxLod + 1> grids;
    std::vector<render::Box> patchBounds; ///< Par parcelle, ligne par ligne.
    nvrhi::ShaderHandle vertexShader;
    nvrhi::ShaderHandle pixelShader;
    nvrhi::ShaderHandle shadowShader;
    nvrhi::InputLayoutHandle inputLayout;
    nvrhi::BindingLayoutHandle layout; ///< space1, les ressources du terrain.
    nvrhi::BufferHandle constants;
    nvrhi::BindingSetHandle bindings;
    nvrhi::GraphicsPipelineHandle pipeline;
    nvrhi::GraphicsPipelineHandle shadowPipeline;
};

/// Crée la passe de `heightmap`, éclairée par `frame`, et enregistre l'envoi de ses textures et de
/// ses grilles dans `commandList`. Ses ombres se dessinent dans l'atlas de `shadows`.
[[nodiscard]] core::Result<TerrainPass> createTerrainPass(nvrhi::IDevice& device,
                                                          nvrhi::ICommandList& commandList,
                                                          const Heightmap& heightmap,
                                                          const render::FrameBindings& frame,
                                                          const render::ShadowPass& shadows);

/// Ce que les dessins du terrain ont soumis et écarté depuis le début.
struct TerrainStats
{
    std::uint64_t drawn = 0;
    std::uint64_t culled = 0;
    std::uint64_t triangles = 0; ///< Ceux des parcelles dessinées, jupes comprises.
};

/// Dessine les parcelles qui touchent le frustum de l'étape, éclairées : l'étape `Opaque`.
void drawTerrain(const render::StageContext& context, const TerrainPass& pass,
                 const Heightmap& heightmap, TerrainStats& stats);

/// Dessine les parcelles dans la cascade de l'étape : l'étape `ShadowCasters`.
void drawTerrainShadow(const render::StageContext& context, const TerrainPass& pass,
                       const Heightmap& heightmap, TerrainStats& stats);

/// Inscrit les deux dessins du terrain dans les étapes du renderer. `pass`, `heightmap` et `stats`
/// doivent vivre aussi longtemps que le renderer : les fonctions les gardent par référence.
void addTerrainPasses(render::RenderStages& stages, const TerrainPass& pass,
                      const Heightmap& heightmap, TerrainStats& cameraStats,
                      TerrainStats& shadowStats);

} // namespace levain::terrain
