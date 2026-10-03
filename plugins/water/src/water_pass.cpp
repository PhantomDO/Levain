#include "levain/water/water_pass.hpp"

#include <array>
#include <utility>
#include <vector>

#include "levain/render/renderer.hpp"
#include "levain/render/shader.hpp"
#include "levain/render/texture.hpp"

namespace levain::water
{

namespace
{

/// La carte de vaguelettes : 256 pixels de côté, répétés tous les quelques mètres.
constexpr std::uint32_t RippleMapSize = 256;
constexpr std::uint32_t RippleSeed = 11;

/// Les constantes du dessin, telles que les lit `plugins/water/shaders/water.slang`.
struct WaterConstants
{
    glm::mat4 viewProjection;
    glm::vec2 center;
    float radius;
    float level;
    float spacing;
    float heightOffset;
    float heightScale;
    float samples;
    float seconds;
    float padding0;
    float padding1;
    float padding2;
};

static_assert(sizeof(WaterConstants) == 112, "disposition lue par water.slang");

/// Le mélange de l'eau sur ce qu'elle recouvre : le shader rend une couleur déjà multipliée par
/// son opacité (premultiplied alpha), et l'image derrière garde la part `1 − alpha`, celle que
/// l'eau laisse passer.
nvrhi::BlendState::RenderTarget premultipliedBlend()
{
    return nvrhi::BlendState::RenderTarget()
        .enableBlend()
        .setSrcBlend(nvrhi::BlendFactor::One)
        .setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
        .setSrcBlendAlpha(nvrhi::BlendFactor::One)
        .setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);
}

nvrhi::GraphicsPipelineHandle createPipeline(nvrhi::IDevice& device, const WaterPass& pass,
                                             const nvrhi::BindingLayoutHandle& frameLayout)
{
    nvrhi::GraphicsPipelineDesc desc;
    desc.primType = nvrhi::PrimitiveType::TriangleList;
    desc.VS = pass.vertexShader;
    desc.PS = pass.pixelShader;
    desc.addBindingLayout(frameLayout);
    desc.addBindingLayout(pass.layout);
    // Caché par le terrain qui dépasse, sans cacher ce qui est derrière lui : l'eau teste la
    // profondeur, mais ne l'écrit pas.
    desc.renderState.depthStencilState.depthTestEnable = true;
    desc.renderState.depthStencilState.depthWriteEnable = false;
    desc.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::Less;
    desc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
    desc.renderState.blendState.targets[0] = premultipliedBlend();
    return device.createGraphicsPipeline(desc, render::sceneTargetInfo());
}

} // namespace

core::Result<WaterPass> createWaterPass(nvrhi::IDevice& device, nvrhi::ICommandList& commandList,
                                        const Lake& lake, const terrain::TerrainPass& terrainPass,
                                        const terrain::Heightmap& heightmap,
                                        const render::FrameBindings& frame)
{
    auto vertexShader = render::loadShader(device, "water.vertexMain", nvrhi::ShaderType::Vertex);
    auto pixelShader = render::loadShader(device, "water.fragmentMain", nvrhi::ShaderType::Pixel);
    if (!vertexShader || !pixelShader)
    {
        return std::unexpected(!vertexShader ? vertexShader.error() : pixelShader.error());
    }
    WaterPass pass;
    pass.lake = lake;
    pass.bounds = {.min = {lake.center.x - lake.radius, lake.level, lake.center.y - lake.radius},
                   .max = {lake.center.x + lake.radius, lake.level, lake.center.y + lake.radius}};
    pass.vertexShader = std::move(*vertexShader);
    pass.pixelShader = std::move(*pixelShader);
    pass.heightmap = terrainPass.heightmap;
    pass.heightSampler = terrainPass.sampler;
    pass.heightOffset = terrainPass.heightOffset;
    pass.heightScale = terrainPass.heightScale;
    pass.spacing = heightmap.spacing;
    pass.samples = static_cast<float>(heightmap.size);

    const std::vector<assets::Image> mips = assets::buildMipChain(
        rippleNormalMapOf(RippleMapSize, RippleSeed), assets::ImageEncoding::Linear);
    std::vector<render::TextureLevel> levels;
    levels.reserve(mips.size());
    for (const assets::Image& mip : mips)
    {
        levels.push_back({.width = mip.width,
                          .height = mip.height,
                          .bytes = std::as_bytes(std::span{mip.rgba})});
    }
    pass.ripples = render::createTexture(device, commandList, levels, "eau : vaguelettes",
                                         nvrhi::Format::RGBA8_UNORM);
    pass.rippleSampler = render::createSampler(
        device, {.maxAnisotropy = 16.0f, .addressMode = nvrhi::SamplerAddressMode::Wrap});

    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::All;
    layoutDesc.setRegisterSpaceAndDescriptorSet(1);
    layoutDesc.bindings = {
        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
        nvrhi::BindingLayoutItem::Texture_SRV(0), nvrhi::BindingLayoutItem::Texture_SRV(1),
        nvrhi::BindingLayoutItem::Sampler(0), nvrhi::BindingLayoutItem::Sampler(1)};
    pass.layout = device.createBindingLayout(layoutDesc);
    pass.constants = device.createBuffer(nvrhi::BufferDesc()
                                             .setByteSize(sizeof(WaterConstants))
                                             .setIsConstantBuffer(true)
                                             .setIsVolatile(true)
                                             .setMaxVersions(render::MaxMeshDrawsPerCommandList)
                                             .setDebugName("eau : constantes"));
    if (!pass.ripples || !pass.rippleSampler || !pass.layout || !pass.constants)
    {
        return core::makeError(core::ErrorCode::InvalidData, "eau refusée par NVRHI");
    }
    pass.bindings = device.createBindingSet(
        nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, pass.constants))
            .addItem(nvrhi::BindingSetItem::Texture_SRV(0, pass.heightmap))
            .addItem(nvrhi::BindingSetItem::Texture_SRV(1, pass.ripples))
            .addItem(nvrhi::BindingSetItem::Sampler(0, pass.heightSampler))
            .addItem(nvrhi::BindingSetItem::Sampler(1, pass.rippleSampler)),
        pass.layout);
    pass.pipeline = createPipeline(device, pass, frame.layout);
    if (!pass.bindings || !pass.pipeline)
    {
        return core::makeError(core::ErrorCode::InvalidData, "pipeline de l'eau refusé");
    }
    return pass;
}

void drawWater(const render::StageContext& context, const WaterPass& pass)
{
    if (render::isOutside(context.frustum, pass.bounds))
    {
        return;
    }
    const WaterConstants constants{
        .viewProjection = context.viewProjection,
        .center = pass.lake.center,
        .radius = pass.lake.radius,
        .level = pass.lake.level,
        .spacing = pass.spacing,
        .heightOffset = pass.heightOffset,
        .heightScale = pass.heightScale,
        .samples = pass.samples,
        // ponytail: des float, précis au millième de seconde pendant 4 h ; un temps ramené dans
        // la période des vaguelettes si une partie dure plus.
        .seconds = static_cast<float>(context.seconds),
        .padding0 = 0.0f,
        .padding1 = 0.0f,
        .padding2 = 0.0f,
    };
    context.commandList.writeBuffer(pass.constants, &constants, sizeof(constants));
    nvrhi::GraphicsState state;
    state.pipeline = pass.pipeline;
    state.framebuffer = &context.target;
    state.viewport.addViewportAndScissorRect(context.target.getFramebufferInfo().getViewport());
    state.addBindingSet(context.frame.bindings).addBindingSet(pass.bindings);
    context.commandList.setGraphicsState(state);
    // Deux triangles, dont le shader tire les sommets de leur indice : pas de vertex buffer.
    nvrhi::DrawArguments arguments;
    arguments.vertexCount = 6;
    context.commandList.draw(arguments);
}

void addWaterPasses(render::RenderStages& stages, const WaterPass& pass)
{
    render::addStageFunction(stages, render::RenderStage::Transparent, "eau",
                             [&pass](const render::StageContext& context)
                             { drawWater(context, pass); });
}

} // namespace levain::water
