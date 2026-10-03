#include "levain/render/triangle.hpp"

#include <format>
#include <utility>

#include "levain/render/shader.hpp"

namespace levain::render
{

core::Result<TrianglePass> createTrianglePass(nvrhi::IDevice& device,
                                              const nvrhi::FramebufferInfo& target)
{
    auto vertexShader = loadShader(device, "triangle.vertexMain", nvrhi::ShaderType::Vertex);
    auto pixelShader = loadShader(device, "triangle.fragmentMain", nvrhi::ShaderType::Pixel);
    if (!vertexShader || !pixelShader)
    {
        return std::unexpected(vertexShader ? pixelShader.error() : vertexShader.error());
    }

    nvrhi::GraphicsPipelineDesc desc;
    desc.primType = nvrhi::PrimitiveType::TriangleList;
    desc.VS = *vertexShader;
    desc.PS = *pixelShader;
    // Ni tampon de profondeur, ni faces à écarter : un seul triangle, vu de face ou non.
    desc.renderState.depthStencilState.depthTestEnable = false;
    desc.renderState.depthStencilState.depthWriteEnable = false;
    desc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;

    nvrhi::GraphicsPipelineHandle pipeline = device.createGraphicsPipeline(desc, target);
    if (!pipeline)
    {
        return core::makeError(core::ErrorCode::InvalidData, "pipeline du triangle refusé");
    }
    return TrianglePass{.vertexShader = std::move(*vertexShader),
                        .pixelShader = std::move(*pixelShader),
                        .pipeline = std::move(pipeline)};
}

void drawTriangle(nvrhi::ICommandList& commandList, const TrianglePass& pass,
                  nvrhi::IFramebuffer& framebuffer)
{
    nvrhi::GraphicsState state;
    state.pipeline = pass.pipeline;
    state.framebuffer = &framebuffer;
    state.viewport.addViewportAndScissorRect(framebuffer.getFramebufferInfo().getViewport());
    commandList.setGraphicsState(state);

    nvrhi::DrawArguments arguments;
    arguments.vertexCount = 3;
    commandList.draw(arguments);
}

} // namespace levain::render
