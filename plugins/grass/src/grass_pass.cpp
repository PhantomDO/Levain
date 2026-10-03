#include "levain/grass/grass_pass.hpp"

#include <array>
#include <span>
#include <utility>

#include "levain/render/renderer.hpp"
#include "levain/render/shader.hpp"
#include "levain/render/texture.hpp"
#include "levain/terrain/patches.hpp"

namespace levain::grass
{

namespace
{

/// Trois trapèzes et la pointe : 7 triangles par brin.
constexpr std::uint32_t BladeSegments = 3;
/// La hauteur du plus grand brin, en mètres : la marge des boîtes des parcelles.
constexpr float TallestBlade = 0.7f;

/// Les constantes d'un dessin, telles que les lit `plugins/grass/shaders/grass.slang`.
struct GrassConstants
{
    glm::mat4 viewProjection;
    glm::vec2 patchOrigin;
    float patchSize;
    float spacing;
    float heightOffset;
    float heightScale;
    float samples;
    float seconds;
    float range; ///< Les brins rapetissent à l'approche de la portée, pour ne pas surgir.
    std::uint32_t
        patchIndex; ///< Une graine par parcelle : deux parcelles n'ont pas les mêmes brins.
    float padding0;
    float padding1;
};

static_assert(sizeof(GrassConstants) == 112, "disposition lue par grass.slang");

/// La distance de la caméra au point le plus proche de la boîte.
float distanceToBox(glm::vec3 point, const render::Box& box)
{
    return glm::distance(point, glm::clamp(point, box.min, box.max));
}

nvrhi::BufferHandle createStaticBuffer(nvrhi::IDevice& device, nvrhi::ICommandList& commandList,
                                       std::span<const std::byte> bytes, bool isIndexBuffer,
                                       const char* name)
{
    nvrhi::BufferHandle buffer = device.createBuffer(
        nvrhi::BufferDesc()
            .setByteSize(bytes.size())
            .setIsVertexBuffer(!isIndexBuffer)
            .setIsIndexBuffer(isIndexBuffer)
            .setInitialState(isIndexBuffer ? nvrhi::ResourceStates::IndexBuffer
                                           : nvrhi::ResourceStates::VertexBuffer)
            .setKeepInitialState(true)
            .setDebugName(name));
    if (buffer)
    {
        commandList.writeBuffer(buffer, bytes.data(), bytes.size());
    }
    return buffer;
}

nvrhi::GraphicsPipelineHandle createPipeline(nvrhi::IDevice& device, const GrassPass& pass,
                                             const nvrhi::BindingLayoutHandle& frameLayout)
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
    // Un brin se voit des deux côtés : le shader retourne la normale de la face arrière.
    desc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
    return device.createGraphicsPipeline(desc, render::sceneTargetInfo());
}

} // namespace

core::Result<GrassPass> createGrassPass(nvrhi::IDevice& device, nvrhi::ICommandList& commandList,
                                        const terrain::TerrainPass& terrainPass,
                                        const terrain::Heightmap& heightmap, float waterLevel,
                                        const render::FrameBindings& frame,
                                        const GrassSettings& settings)
{
    auto vertexShader = render::loadShader(device, "grass.vertexMain", nvrhi::ShaderType::Vertex);
    auto pixelShader = render::loadShader(device, "grass.fragmentMain", nvrhi::ShaderType::Pixel);
    if (!vertexShader || !pixelShader)
    {
        return std::unexpected(!vertexShader ? vertexShader.error() : pixelShader.error());
    }
    GrassPass pass;
    pass.settings = settings;
    pass.vertexShader = std::move(*vertexShader);
    pass.pixelShader = std::move(*pixelShader);
    pass.heightmap = terrainPass.heightmap;
    pass.sampler = terrainPass.sampler;
    pass.heightOffset = terrainPass.heightOffset;
    pass.heightScale = terrainPass.heightScale;
    pass.spacing = heightmap.spacing;
    pass.samples = static_cast<float>(heightmap.size);
    pass.patchSize = static_cast<float>(terrain::PatchQuads) * heightmap.spacing;
    pass.patchesPerSide = terrain::patchesPerSide(heightmap);
    pass.patchBounds = terrainPass.patchBounds;
    for (render::Box& bounds : pass.patchBounds)
    {
        bounds.max.y += TallestBlade;
    }

    const BladeGeometry blade = bladeGeometryOf(BladeSegments);
    pass.vertices = createStaticBuffer(
        device, commandList, std::as_bytes(std::span{blade.vertices}), false, "herbe : brin");
    pass.indices = createStaticBuffer(device, commandList, std::as_bytes(std::span{blade.indices}),
                                      true, "herbe : triangles du brin");
    pass.indexCount = static_cast<std::uint32_t>(blade.indices.size());
    const std::vector<std::uint8_t> density = densityMapOf(heightmap, waterLevel);
    const std::array densityLevel{render::TextureLevel{.width = heightmap.size,
                                                       .height = heightmap.size,
                                                       .bytes = std::as_bytes(std::span{density})}};
    pass.density = render::createTexture(device, commandList, densityLevel, "herbe : densité",
                                         nvrhi::Format::R8_UNORM);

    const nvrhi::VertexAttributeDesc position = nvrhi::VertexAttributeDesc()
                                                    .setName("POSITION")
                                                    .setFormat(nvrhi::Format::RG32_FLOAT)
                                                    .setOffset(0)
                                                    .setElementStride(sizeof(glm::vec2));
    pass.inputLayout = device.createInputLayout(&position, 1, pass.vertexShader);
    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::All;
    layoutDesc.setRegisterSpaceAndDescriptorSet(1);
    layoutDesc.bindings = {nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                           nvrhi::BindingLayoutItem::Texture_SRV(0),
                           nvrhi::BindingLayoutItem::Texture_SRV(1),
                           nvrhi::BindingLayoutItem::Sampler(0)};
    pass.layout = device.createBindingLayout(layoutDesc);
    // Une version par parcelle dessinée.
    pass.constants = device.createBuffer(nvrhi::BufferDesc()
                                             .setByteSize(sizeof(GrassConstants))
                                             .setIsConstantBuffer(true)
                                             .setIsVolatile(true)
                                             .setMaxVersions(render::MaxMeshDrawsPerCommandList)
                                             .setDebugName("herbe : constantes"));
    if (!pass.vertices || !pass.indices || !pass.density || !pass.inputLayout || !pass.layout ||
        !pass.constants)
    {
        return core::makeError(core::ErrorCode::InvalidData, "herbe refusée par NVRHI");
    }
    pass.bindings = device.createBindingSet(
        nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, pass.constants))
            .addItem(nvrhi::BindingSetItem::Texture_SRV(0, pass.heightmap))
            .addItem(nvrhi::BindingSetItem::Texture_SRV(1, pass.density))
            .addItem(nvrhi::BindingSetItem::Sampler(0, pass.sampler)),
        pass.layout);
    pass.pipeline = createPipeline(device, pass, frame.layout);
    if (!pass.bindings || !pass.pipeline)
    {
        return core::makeError(core::ErrorCode::InvalidData, "pipeline de l'herbe refusé");
    }
    return pass;
}

void drawGrass(const render::StageContext& context, const GrassPass& pass, GrassStats& stats)
{
    const float area = pass.patchSize * pass.patchSize;
    for (std::uint32_t z = 0; z < pass.patchesPerSide; ++z)
    {
        for (std::uint32_t x = 0; x < pass.patchesPerSide; ++x)
        {
            const std::uint32_t index = (z * pass.patchesPerSide) + x;
            const render::Box& bounds = pass.patchBounds[index];
            const std::uint32_t blades =
                bladeCountOf(distanceToBox(context.cameraPosition, bounds), area, pass.settings);
            if (blades == 0 || render::isOutside(context.frustum, bounds))
            {
                continue;
            }
            ++stats.patches;
            stats.blades += blades;
            const GrassConstants constants{
                .viewProjection = context.viewProjection,
                .patchOrigin = glm::vec2{x, z} * pass.patchSize,
                .patchSize = pass.patchSize,
                .spacing = pass.spacing,
                .heightOffset = pass.heightOffset,
                .heightScale = pass.heightScale,
                .samples = pass.samples,
                // ponytail: des float, précis au millième de seconde pendant 4 h, comme l'eau.
                .seconds = static_cast<float>(context.seconds),
                .range = pass.settings.range,
                .patchIndex = index,
                .padding0 = 0.0f,
                .padding1 = 0.0f,
            };
            context.commandList.writeBuffer(pass.constants, &constants, sizeof(constants));
            nvrhi::GraphicsState state;
            state.pipeline = pass.pipeline;
            state.framebuffer = &context.target;
            state.viewport.addViewportAndScissorRect(
                context.target.getFramebufferInfo().getViewport());
            state.addBindingSet(context.frame.bindings).addBindingSet(pass.bindings);
            state.addVertexBuffer(
                nvrhi::VertexBufferBinding().setBuffer(pass.vertices).setSlot(0).setOffset(0));
            state.setIndexBuffer(nvrhi::IndexBufferBinding()
                                     .setBuffer(pass.indices)
                                     .setFormat(nvrhi::Format::R16_UINT)
                                     .setOffset(0));
            context.commandList.setGraphicsState(state);
            nvrhi::DrawArguments arguments;
            arguments.vertexCount = pass.indexCount;
            arguments.instanceCount = blades;
            context.commandList.drawIndexed(arguments);
        }
    }
}

void addGrassPasses(render::RenderStages& stages, const GrassPass& pass, GrassStats& stats)
{
    render::addStageFunction(stages, render::RenderStage::Opaque, "herbe",
                             [&pass, &stats](const render::StageContext& context)
                             { drawGrass(context, pass, stats); });
}

} // namespace levain::grass
