#include "levain/render/sky.hpp"

#include <utility>

#include <glm/glm.hpp>

#include "levain/render/shader.hpp"

namespace levain::render
{

namespace
{

/// Les réglages, tels que les lit `shaders/sky.slang`.
struct SkyConstants
{
    glm::mat4 inverseViewProjection;
    float intensity;
    float padding0;
    float padding1;
    float padding2;
};

static_assert(sizeof(SkyConstants) == 80, "disposition lue par shaders/sky.slang");

/// Les ciels qu'une command list peut dessiner : un par vue.
constexpr std::uint32_t MaxSkiesPerCommandList = 4;

} // namespace

core::Result<SkyPass> createSkyPass(nvrhi::IDevice& device, const nvrhi::FramebufferInfo& target,
                                    const Environment& environment)
{
    auto vertexShader = loadShader(device, "sky.vertexMain", nvrhi::ShaderType::Vertex);
    auto pixelShader = loadShader(device, "sky.fragmentMain", nvrhi::ShaderType::Pixel);
    if (!vertexShader || !pixelShader)
    {
        return std::unexpected(vertexShader ? pixelShader.error() : vertexShader.error());
    }
    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::Pixel;
    layoutDesc.bindings = {nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                           nvrhi::BindingLayoutItem::Texture_SRV(0),
                           nvrhi::BindingLayoutItem::Sampler(0)};

    SkyPass pass;
    pass.vertexShader = std::move(*vertexShader);
    pass.pixelShader = std::move(*pixelShader);
    pass.layout = device.createBindingLayout(layoutDesc);
    nvrhi::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.primType = nvrhi::PrimitiveType::TriangleList;
    pipelineDesc.VS = pass.vertexShader;
    pipelineDesc.PS = pass.pixelShader;
    pipelineDesc.addBindingLayout(pass.layout);
    // LessEqual : le triangle est au plan lointain, à la profondeur où le depth buffer a été
    // effacé. Il passe là, et nulle part où un mesh est plus près.
    pipelineDesc.renderState.depthStencilState.depthTestEnable = true;
    pipelineDesc.renderState.depthStencilState.depthWriteEnable = false;
    pipelineDesc.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::LessOrEqual;
    pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
    pass.pipeline = device.createGraphicsPipeline(pipelineDesc, target);
    pass.constants = device.createBuffer(nvrhi::BufferDesc()
                                             .setByteSize(sizeof(SkyConstants))
                                             .setIsConstantBuffer(true)
                                             .setIsVolatile(true)
                                             .setMaxVersions(MaxSkiesPerCommandList)
                                             .setDebugName("constantes du ciel"));
    const nvrhi::SamplerHandle sampler = device.createSampler(nvrhi::SamplerDesc());
    if (!pass.layout || !pass.pipeline || !pass.constants || !sampler)
    {
        return core::makeError(core::ErrorCode::InvalidData, "passe du ciel refusée par NVRHI");
    }
    // Le binding set garde le sampler en vie.
    pass.bindings = device.createBindingSet(
        nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, pass.constants))
            .addItem(nvrhi::BindingSetItem::Texture_SRV(0, environment.cube))
            .addItem(nvrhi::BindingSetItem::Sampler(0, sampler)),
        pass.layout);
    return pass;
}

void drawSky(nvrhi::ICommandList& commandList, const SkyPass& pass, nvrhi::IFramebuffer& target,
             const Camera& camera, float aspectRatio, float intensity)
{
    const glm::mat4 rotation{glm::mat3{viewOf(camera)}};
    const SkyConstants constants{
        .inverseViewProjection = glm::inverse(projectionOf(camera, aspectRatio) * rotation),
        .intensity = intensity,
        .padding0 = 0.0f,
        .padding1 = 0.0f,
        .padding2 = 0.0f,
    };
    commandList.writeBuffer(pass.constants, &constants, sizeof(constants));
    nvrhi::GraphicsState state;
    state.pipeline = pass.pipeline;
    state.framebuffer = &target;
    state.viewport.addViewportAndScissorRect(target.getFramebufferInfo().getViewport());
    state.addBindingSet(pass.bindings);
    commandList.setGraphicsState(state);
    nvrhi::DrawArguments arguments;
    arguments.vertexCount = 3;
    commandList.draw(arguments);
}

} // namespace levain::render
