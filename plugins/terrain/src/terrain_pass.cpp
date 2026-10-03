#include "levain/terrain/terrain_pass.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include <glm/gtc/packing.hpp>
#include <glm/gtc/type_precision.hpp>

#include "levain/render/shader.hpp"
#include "levain/render/texture.hpp"
#include "levain/terrain/layers.hpp"

namespace levain::terrain
{

namespace
{

/// Jusqu'à 64 m de la caméra, un sommet par mètre ; un niveau de plus à chaque doublement.
constexpr float Lod0Distance = 64.0f;

/// Les constantes d'un dessin, telles que les lit `plugins/terrain/shaders/terrain.slang`.
struct TerrainConstants
{
    glm::mat4 viewProjection;
    glm::vec2 patchOrigin;
    float patchSize;
    float spacing;
    float heightOffset;
    float heightScale;
    float samples;
    float skirtDepth; ///< De combien la jupe descend sous le bord.
};

static_assert(sizeof(TerrainConstants) == 96, "disposition lue par terrain.slang");

/// La grille d'une parcelle à `quads` intervalles par côté, avec sa jupe (`patchGeometryOf`).
PatchGrid createPatchGrid(nvrhi::IDevice& device, nvrhi::ICommandList& commandList,
                          std::uint32_t quads)
{
    const PatchGeometry geometry = patchGeometryOf(quads);
    const std::span vertices{geometry.vertices};
    const std::span indices{geometry.indices};
    PatchGrid grid{
        .vertices = device.createBuffer(nvrhi::BufferDesc()
                                            .setByteSize(vertices.size_bytes())
                                            .setIsVertexBuffer(true)
                                            .setInitialState(nvrhi::ResourceStates::VertexBuffer)
                                            .setKeepInitialState(true)
                                            .setDebugName("terrain : grille")),
        .indices = device.createBuffer(nvrhi::BufferDesc()
                                           .setByteSize(indices.size_bytes())
                                           .setIsIndexBuffer(true)
                                           .setInitialState(nvrhi::ResourceStates::IndexBuffer)
                                           .setKeepInitialState(true)
                                           .setDebugName("terrain : triangles")),
        .indexCount = static_cast<std::uint32_t>(indices.size()),
    };
    commandList.writeBuffer(grid.vertices, vertices.data(), vertices.size_bytes());
    commandList.writeBuffer(grid.indices, indices.data(), indices.size_bytes());
    return grid;
}

/// Les hauteurs ramenées de 0 à 1, en flottants 16 bits : un pas de 0,05 % au plus, moins de 5 cm
/// sur 91 m. Ni un R32 flottant (qui ne se filtre pas sans extension) ni un R16 normalisé (absent)
/// n'existent dans le cœur de WebGPU.
std::vector<std::uint16_t> normalizedHeightsOf(const Heightmap& heightmap, float offset,
                                               float scale)
{
    std::vector<std::uint16_t> heights(heightmap.heights.size());
    std::ranges::transform(heightmap.heights, heights.begin(), [offset, scale](float height)
                           { return glm::packHalf1x16((height - offset) / scale); });
    return heights;
}

nvrhi::GraphicsPipelineHandle createPipeline(nvrhi::IDevice& device, const TerrainPass& pass,
                                             const nvrhi::BindingLayoutHandle& frameLayout,
                                             const nvrhi::FramebufferInfo& target)
{
    nvrhi::GraphicsPipelineDesc desc;
    desc.primType = nvrhi::PrimitiveType::TriangleList;
    desc.inputLayout = pass.inputLayout;
    desc.VS = pass.vertexShader;
    desc.PS = pass.pixelShader;
    desc.addBindingLayout(frameLayout);
    desc.addBindingLayout(pass.layout);
    desc.renderState.depthStencilState.depthTestEnable = true;
    desc.renderState.depthStencilState.depthWriteEnable = true;
    desc.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::Less;
    // Les deux faces : une jupe se voit de l'une ou de l'autre, selon le côté de la fente.
    desc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
    return device.createGraphicsPipeline(desc, target);
}

/// Le même réglage que la passe d'ombres des meshes (shadows.cpp) : le biais selon la pente vue du
/// soleil, et une part constante non nulle, sans laquelle NVRHI ignore le biais sous Vulkan.
nvrhi::GraphicsPipelineHandle createShadowPipeline(nvrhi::IDevice& device, const TerrainPass& pass,
                                                   const render::ShadowPass& shadows)
{
    nvrhi::GraphicsPipelineDesc desc;
    desc.primType = nvrhi::PrimitiveType::TriangleList;
    desc.inputLayout = pass.inputLayout;
    desc.VS = pass.shadowShader;
    desc.addBindingLayout(pass.layout);
    desc.renderState.depthStencilState.depthTestEnable = true;
    desc.renderState.depthStencilState.depthWriteEnable = true;
    desc.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::Less;
    desc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
    desc.renderState.rasterState.slopeScaledDepthBias = 2.0f;
    desc.renderState.rasterState.depthBias = 1;
    return device.createGraphicsPipeline(desc, shadows.framebuffer->getFramebufferInfo());
}

/// Les parcelles du terrain, chacune avec ses constantes, sauf celles hors du frustum de l'étape.
template <typename Draw>
void forEachVisiblePatch(const render::StageContext& context, const TerrainPass& pass,
                         const Heightmap& heightmap, TerrainStats& stats, Draw&& draw)
{
    const std::uint32_t side = patchesPerSide(heightmap);
    const float patchSize = static_cast<float>(PatchQuads) * heightmap.spacing;
    for (std::uint32_t z = 0; z < side; ++z)
    {
        for (std::uint32_t x = 0; x < side; ++x)
        {
            if (render::isOutside(context.frustum, pass.patchBounds[(z * side) + x]))
            {
                ++stats.culled;
                continue;
            }
            ++stats.drawn;
            const glm::vec2 center = (glm::vec2{x, z} + 0.5f) * patchSize;
            const glm::vec3 centerPoint{center.x, heightAt(heightmap, center), center.y};
            const std::uint32_t lod =
                lodOf(glm::distance(centerPoint, context.cameraPosition), Lod0Distance);
            const PatchGrid& grid = pass.grids[lod];
            stats.triangles += grid.indexCount / 3;
            const TerrainConstants constants{
                .viewProjection = context.viewProjection,
                .patchOrigin = glm::vec2{x, z} * patchSize,
                .patchSize = patchSize,
                .spacing = heightmap.spacing,
                .heightOffset = pass.heightOffset,
                .heightScale = pass.heightScale,
                .samples = static_cast<float>(heightmap.size),
                // Deux fois l'écart entre deux sommets : ce qu'un niveau plus grossier peut
                // manquer sur un bord, dans une pente à 45°.
                .skirtDepth = 2.0f * heightmap.spacing * static_cast<float>(1u << lod),
            };
            context.commandList.writeBuffer(pass.constants, &constants, sizeof(constants));
            draw(grid);
        }
    }
}

void setGrid(nvrhi::GraphicsState& state, const PatchGrid& grid)
{
    state.addVertexBuffer(
        nvrhi::VertexBufferBinding().setBuffer(grid.vertices).setSlot(0).setOffset(0));
    state.setIndexBuffer(nvrhi::IndexBufferBinding()
                             .setBuffer(grid.indices)
                             .setFormat(nvrhi::Format::R32_UINT)
                             .setOffset(0));
}

} // namespace

core::Result<TerrainPass> createTerrainPass(nvrhi::IDevice& device,
                                            nvrhi::ICommandList& commandList,
                                            const Heightmap& heightmap,
                                            const render::FrameBindings& frame,
                                            const render::ShadowPass& shadows)
{
    auto vertexShader = render::loadShader(device, "terrain.vertexMain", nvrhi::ShaderType::Vertex);
    auto pixelShader = render::loadShader(device, "terrain.fragmentMain", nvrhi::ShaderType::Pixel);
    auto shadowShader = render::loadShader(device, "terrain.shadowMain", nvrhi::ShaderType::Vertex);
    if (!vertexShader || !pixelShader || !shadowShader)
    {
        return std::unexpected(!vertexShader  ? vertexShader.error()
                               : !pixelShader ? pixelShader.error()
                                              : shadowShader.error());
    }
    const auto [lowest, highest] = std::ranges::minmax(heightmap.heights);
    TerrainPass pass;
    pass.heightOffset = lowest;
    pass.heightScale = std::max(highest - lowest, 1e-3f);
    pass.vertexShader = std::move(*vertexShader);
    pass.pixelShader = std::move(*pixelShader);
    pass.shadowShader = std::move(*shadowShader);

    const std::vector<std::uint16_t> heights =
        normalizedHeightsOf(heightmap, pass.heightOffset, pass.heightScale);
    const std::array heightLevel{render::TextureLevel{.width = heightmap.size,
                                                      .height = heightmap.size,
                                                      .bytes = std::as_bytes(std::span{heights})}};
    pass.heightmap = render::createTexture(device, commandList, heightLevel, "terrain : hauteurs",
                                           nvrhi::Format::R16_FLOAT);
    const std::vector<glm::u8vec4> weights = weightMapOf(heightmap);
    const std::array weightLevel{render::TextureLevel{.width = heightmap.size,
                                                      .height = heightmap.size,
                                                      .bytes = std::as_bytes(std::span{weights})}};
    pass.weights = render::createTexture(device, commandList, weightLevel, "terrain : poids",
                                         nvrhi::Format::RGBA8_UNORM);
    pass.sampler = device.createSampler(nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(
        nvrhi::SamplerAddressMode::Clamp));
    for (std::uint32_t lod = 0; lod <= MaxLod; ++lod)
    {
        pass.grids[lod] = createPatchGrid(device, commandList, PatchQuads >> lod);
    }
    const std::uint32_t side = patchesPerSide(heightmap);
    for (std::uint32_t z = 0; z < side; ++z)
    {
        for (std::uint32_t x = 0; x < side; ++x)
        {
            pass.patchBounds.push_back(patchBoundsOf(heightmap, {x, z}));
        }
    }

    const nvrhi::VertexAttributeDesc position = nvrhi::VertexAttributeDesc()
                                                    .setName("POSITION")
                                                    .setFormat(nvrhi::Format::RGB32_FLOAT)
                                                    .setOffset(0)
                                                    .setElementStride(sizeof(GridVertex));
    pass.inputLayout = device.createInputLayout(&position, 1, pass.vertexShader);
    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::All;
    layoutDesc.setRegisterSpaceAndDescriptorSet(1);
    layoutDesc.bindings = {nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                           nvrhi::BindingLayoutItem::Texture_SRV(0),
                           nvrhi::BindingLayoutItem::Texture_SRV(1),
                           nvrhi::BindingLayoutItem::Sampler(0)};
    pass.layout = device.createBindingLayout(layoutDesc);
    // Une version par parcelle dessinée, dans l'image et dans chaque cascade.
    pass.constants = device.createBuffer(nvrhi::BufferDesc()
                                             .setByteSize(sizeof(TerrainConstants))
                                             .setIsConstantBuffer(true)
                                             .setIsVolatile(true)
                                             .setMaxVersions(render::MaxMeshDrawsPerCommandList)
                                             .setDebugName("terrain : constantes"));
    if (!pass.heightmap || !pass.weights || !pass.sampler || !pass.inputLayout || !pass.layout ||
        !pass.constants)
    {
        return core::makeError(core::ErrorCode::InvalidData, "terrain refusé par NVRHI");
    }
    pass.bindings = device.createBindingSet(
        nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, pass.constants))
            .addItem(nvrhi::BindingSetItem::Texture_SRV(0, pass.heightmap))
            .addItem(nvrhi::BindingSetItem::Texture_SRV(1, pass.weights))
            .addItem(nvrhi::BindingSetItem::Sampler(0, pass.sampler)),
        pass.layout);
    pass.pipeline = createPipeline(device, pass, frame.layout, render::sceneTargetInfo());
    pass.shadowPipeline = createShadowPipeline(device, pass, shadows);
    if (!pass.bindings || !pass.pipeline || !pass.shadowPipeline)
    {
        return core::makeError(core::ErrorCode::InvalidData, "pipelines du terrain refusés");
    }
    return pass;
}

void drawTerrain(const render::StageContext& context, const TerrainPass& pass,
                 const Heightmap& heightmap, TerrainStats& stats)
{
    forEachVisiblePatch(
        context, pass, heightmap, stats,
        [&](const PatchGrid& grid)
        {
            nvrhi::GraphicsState state;
            state.pipeline = pass.pipeline;
            state.framebuffer = &context.target;
            state.viewport.addViewportAndScissorRect(
                context.target.getFramebufferInfo().getViewport());
            state.addBindingSet(context.frame.bindings).addBindingSet(pass.bindings);
            setGrid(state, grid);
            context.commandList.setGraphicsState(state);
            nvrhi::DrawArguments arguments;
            arguments.vertexCount = grid.indexCount;
            context.commandList.drawIndexed(arguments);
        });
}

void drawTerrainShadow(const render::StageContext& context, const TerrainPass& pass,
                       const Heightmap& heightmap, TerrainStats& stats)
{
    // Le quart de l'atlas de la cascade, comme `drawShadowCaster`.
    const glm::uvec2 cell = render::atlasCellOf(context.cascade) * context.shadows.resolution;
    const auto size = static_cast<float>(context.shadows.resolution);
    forEachVisiblePatch(context, pass, heightmap, stats,
                        [&](const PatchGrid& grid)
                        {
                            nvrhi::GraphicsState state;
                            state.pipeline = pass.shadowPipeline;
                            state.framebuffer = context.shadows.framebuffer;
                            state.viewport.addViewportAndScissorRect(nvrhi::Viewport(
                                static_cast<float>(cell.x), static_cast<float>(cell.x) + size,
                                static_cast<float>(cell.y), static_cast<float>(cell.y) + size, 0.0f,
                                1.0f));
                            state.addBindingSet(pass.bindings);
                            setGrid(state, grid);
                            context.commandList.setGraphicsState(state);
                            nvrhi::DrawArguments arguments;
                            arguments.vertexCount = grid.indexCount;
                            context.commandList.drawIndexed(arguments);
                        });
}

void addTerrainPasses(render::RenderStages& stages, const TerrainPass& pass,
                      const Heightmap& heightmap, TerrainStats& cameraStats,
                      TerrainStats& shadowStats)
{
    render::addStageFunction(stages, render::RenderStage::ShadowCasters, "terrain",
                             [&pass, &heightmap, &shadowStats](const render::StageContext& context)
                             { drawTerrainShadow(context, pass, heightmap, shadowStats); });
    render::addStageFunction(stages, render::RenderStage::Opaque, "terrain",
                             [&pass, &heightmap, &cameraStats](const render::StageContext& context)
                             { drawTerrain(context, pass, heightmap, cameraStats); });
}

} // namespace levain::terrain
