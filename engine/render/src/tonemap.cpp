#include "levain/render/tonemap.hpp"

#include <utility>

#include "shader.hpp"

namespace levain::render
{

namespace
{

/// Les réglages, tels que les lit `shaders/tonemap.slang`.
struct TonemapConstants
{
    float exposure;
    float padding0;
    float padding1;
    float padding2;
};

static_assert(sizeof(TonemapConstants) == 16, "disposition lue par shaders/tonemap.slang");

/// Les passes qu'une command list peut enregistrer : une par image affichée.
constexpr std::uint32_t MaxTonemapsPerCommandList = 4;

} // namespace

core::Result<TonemapPass> createTonemapPass(nvrhi::IDevice& device,
                                            const nvrhi::FramebufferInfo& output)
{
    auto vertexShader = loadShader(device, "tonemap.vertexMain", nvrhi::ShaderType::Vertex);
    auto pixelShader = loadShader(device, "tonemap.fragmentMain", nvrhi::ShaderType::Pixel);
    if (!vertexShader || !pixelShader)
    {
        return std::unexpected(vertexShader ? pixelShader.error() : vertexShader.error());
    }
    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::Pixel;
    layoutDesc.bindings = {nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                           nvrhi::BindingLayoutItem::Texture_SRV(0),
                           nvrhi::BindingLayoutItem::Sampler(0)};

    TonemapPass pass;
    pass.vertexShader = std::move(*vertexShader);
    pass.pixelShader = std::move(*pixelShader);
    pass.layout = device.createBindingLayout(layoutDesc);
    nvrhi::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.primType = nvrhi::PrimitiveType::TriangleList;
    pipelineDesc.VS = pass.vertexShader;
    pipelineDesc.PS = pass.pixelShader;
    pipelineDesc.addBindingLayout(pass.layout);
    pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
    pipelineDesc.renderState.depthStencilState.depthWriteEnable = false;
    pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
    pass.pipeline = device.createGraphicsPipeline(pipelineDesc, output);
    pass.constants = device.createBuffer(nvrhi::BufferDesc()
                                             .setByteSize(sizeof(TonemapConstants))
                                             .setIsConstantBuffer(true)
                                             .setIsVolatile(true)
                                             .setMaxVersions(MaxTonemapsPerCommandList)
                                             .setDebugName("constantes du tonemapping"));
    // Un texel de l'image HDR par pixel affiché : pas de filtrage à faire.
    pass.sampler =
        device.createSampler(nvrhi::SamplerDesc().setAllFilters(false).setAllAddressModes(
            nvrhi::SamplerAddressMode::Clamp));
    if (!pass.layout || !pass.pipeline || !pass.constants || !pass.sampler)
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               "passe de tonemapping refusée par NVRHI");
    }
    return pass;
}

nvrhi::ITexture* ensureHdrTarget(nvrhi::IDevice& device, const TonemapPass& pass, HdrTarget& target,
                                 std::uint32_t width, std::uint32_t height)
{
    if (target.texture && target.texture->getDesc().width == width &&
        target.texture->getDesc().height == height)
    {
        return target.texture;
    }
    // Dessinée par la scène, puis lue par la passe : NVRHI place la transition entre les deux
    // (suivi automatique des états).
    target.texture =
        device.createTexture(nvrhi::TextureDesc()
                                 .setWidth(width)
                                 .setHeight(height)
                                 .setFormat(HdrFormat)
                                 .setIsRenderTarget(true)
                                 .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                 .setKeepInitialState(true)
                                 .setDebugName("image HDR"));
    target.bindings = device.createBindingSet(
        nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, pass.constants))
            .addItem(nvrhi::BindingSetItem::Texture_SRV(0, target.texture))
            .addItem(nvrhi::BindingSetItem::Sampler(0, pass.sampler)),
        pass.layout);
    return target.texture;
}

void tonemap(nvrhi::ICommandList& commandList, const TonemapPass& pass, const HdrTarget& target,
             nvrhi::IFramebuffer& output, const TonemapSettings& settings)
{
    const TonemapConstants constants{
        .exposure = settings.exposure, .padding0 = 0.0f, .padding1 = 0.0f, .padding2 = 0.0f};
    commandList.writeBuffer(pass.constants, &constants, sizeof(constants));
    nvrhi::GraphicsState state;
    state.pipeline = pass.pipeline;
    state.framebuffer = &output;
    state.viewport.addViewportAndScissorRect(output.getFramebufferInfo().getViewport());
    state.addBindingSet(target.bindings);
    commandList.setGraphicsState(state);
    nvrhi::DrawArguments arguments;
    arguments.vertexCount = 3;
    commandList.draw(arguments);
}

} // namespace levain::render
