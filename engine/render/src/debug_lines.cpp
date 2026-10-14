#include "levain/render/debug_lines.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <mutex>
#include <utility>
#include <vector>

#include "levain/core/log.hpp"
#include "levain/render/shader.hpp"

namespace levain::render
{

namespace
{

/// Un sommet, tel que le lit `shaders/debug_lines.slang`.
struct DebugVertex
{
    glm::vec3 position;
    glm::vec4 color; ///< rgb en lumière linéaire, a l'opacité (1 en `DebugBlend::Opaque`)
};

static_assert(sizeof(DebugVertex) == 28, "disposition lue par shaders/debug_lines.slang");

/// Les versions des constantes : une par image en vol (deux, `swapchain_vk.cpp`), avec de la marge.
/// Une version n'est libérée qu'à la fin de la soumission qui l'a écrite.
constexpr std::uint32_t ConstantVersions = 4;

/// Le dépassement de `MaxDebugLines` se répète à chaque image : le dire une fois suffit.
void warnOnceTooManyLines(std::size_t requested)
{
    static std::once_flag warned;
    std::call_once(warned,
                   [requested]
                   {
                       core::log("render", core::LogLevel::Warning,
                                 "{} lignes de debug demandées, {} dessinées : le reste est perdu "
                                 "(dit une seule fois)",
                                 requested, MaxDebugLines);
                   });
}

} // namespace

core::Result<DebugLinesPass> createDebugLinesPass(nvrhi::IDevice& device,
                                                  const nvrhi::FramebufferInfo& target)
{
    auto vertexShader = loadShader(device, "debug_lines.vertexMain", nvrhi::ShaderType::Vertex);
    auto pixelShader = loadShader(device, "debug_lines.fragmentMain", nvrhi::ShaderType::Pixel);
    if (!vertexShader || !pixelShader)
    {
        return std::unexpected(vertexShader ? pixelShader.error() : vertexShader.error());
    }
    DebugLinesPass pass;
    pass.vertexShader = std::move(*vertexShader);
    pass.pixelShader = std::move(*pixelShader);
    // Les noms sont les sémantiques de VertexInput, dans le même ordre (le backend WebGPU numérote
    // les attributs ainsi).
    const std::array<nvrhi::VertexAttributeDesc, 2> attributes{
        nvrhi::VertexAttributeDesc()
            .setName("POSITION")
            .setFormat(nvrhi::Format::RGB32_FLOAT)
            .setOffset(offsetof(DebugVertex, position))
            .setElementStride(sizeof(DebugVertex)),
        nvrhi::VertexAttributeDesc()
            .setName("COLOR")
            .setFormat(nvrhi::Format::RGBA32_FLOAT)
            .setOffset(offsetof(DebugVertex, color))
            .setElementStride(sizeof(DebugVertex)),
    };
    pass.inputLayout =
        device.createInputLayout(attributes.data(), attributes.size(), pass.vertexShader);
    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::Vertex;
    layoutDesc.bindings = {nvrhi::BindingLayoutItem::VolatileConstantBuffer(0)};
    pass.layout = device.createBindingLayout(layoutDesc);

    nvrhi::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.primType = nvrhi::PrimitiveType::LineList;
    pipelineDesc.inputLayout = pass.inputLayout;
    pipelineDesc.VS = pass.vertexShader;
    pipelineDesc.PS = pass.pixelShader;
    pipelineDesc.addBindingLayout(pass.layout);
    // Testées contre la profondeur de la scène, sans l'écrire : une ligne ne cache rien.
    pipelineDesc.renderState.depthStencilState.depthTestEnable = true;
    pipelineDesc.renderState.depthStencilState.depthWriteEnable = false;
    pipelineDesc.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::LessOrEqual;
    pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
    pass.pipeline = device.createGraphicsPipeline(pipelineDesc, target);
    pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
    pass.onTopPipeline = device.createGraphicsPipeline(pipelineDesc, target);
    // Les mêmes, mêlées par l'opacité : source * a + fond * (1 - a), l'alpha de l'image HDR gardé.
    nvrhi::BlendState::RenderTarget& blend = pipelineDesc.renderState.blendState.targets[0];
    blend.blendEnable = true;
    blend.srcBlend = nvrhi::BlendFactor::SrcAlpha;
    blend.destBlend = nvrhi::BlendFactor::InvSrcAlpha;
    blend.blendOp = nvrhi::BlendOp::Add;
    blend.srcBlendAlpha = nvrhi::BlendFactor::Zero;
    blend.destBlendAlpha = nvrhi::BlendFactor::One;
    blend.blendOpAlpha = nvrhi::BlendOp::Add;
    pass.blendedOnTopPipeline = device.createGraphicsPipeline(pipelineDesc, target);
    pipelineDesc.renderState.depthStencilState.depthTestEnable = true;
    pass.blendedPipeline = device.createGraphicsPipeline(pipelineDesc, target);
    pass.constants = device.createBuffer(nvrhi::BufferDesc()
                                             .setByteSize(sizeof(glm::mat4))
                                             .setIsConstantBuffer(true)
                                             .setIsVolatile(true)
                                             .setMaxVersions(ConstantVersions)
                                             .setDebugName("constantes des lignes de debug"));
    pass.vertices = device.createBuffer(nvrhi::BufferDesc()
                                            .setByteSize(sizeof(DebugVertex) * 2 * MaxDebugLines)
                                            .setIsVertexBuffer(true)
                                            .setInitialState(nvrhi::ResourceStates::VertexBuffer)
                                            .setKeepInitialState(true)
                                            .setDebugName("lignes de debug"));
    if (!pass.inputLayout || !pass.layout || !pass.pipeline || !pass.onTopPipeline ||
        !pass.blendedPipeline || !pass.blendedOnTopPipeline || !pass.constants || !pass.vertices)
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               "passe des lignes de debug refusée par NVRHI");
    }
    pass.bindings = device.createBindingSet(
        nvrhi::BindingSetDesc().addItem(nvrhi::BindingSetItem::ConstantBuffer(0, pass.constants)),
        pass.layout);
    return pass;
}

std::optional<DebugLine> clipToFrustum(const DebugLine& line, const Frustum& frustum)
{
    // Coupe paramétrique (Cyrus-Beck) : p(t) = from + t (to - from), t dans [enter, leave] ; chaque
    // plan intérieur en retire ce qui est de son mauvais côté. Les distances signées sont celles
    // des bouts ; entre les deux elles varient linéairement.
    float enter = 0.0f;
    float leave = 1.0f;
    for (const glm::vec4& plane : frustum.planes)
    {
        const glm::vec3 normal{plane};
        const float distanceFrom = glm::dot(normal, line.from) + plane.w;
        const float distanceTo = glm::dot(normal, line.to) + plane.w;
        if (distanceFrom < 0.0f && distanceTo < 0.0f)
        {
            return std::nullopt; // tout du mauvais côté de ce plan
        }
        if (distanceFrom < 0.0f)
        {
            enter = std::max(enter, distanceFrom / (distanceFrom - distanceTo));
        }
        else if (distanceTo < 0.0f)
        {
            leave = std::min(leave, distanceFrom / (distanceFrom - distanceTo));
        }
    }
    if (enter >= leave)
    {
        return std::nullopt; // la droite passe à côté du volume (un coin)
    }
    const auto at = [&line](float t)
    {
        return std::pair{glm::mix(line.from, line.to, t),
                         glm::mix(line.alphaFrom, line.alphaTo, t)};
    };
    const auto [from, alphaFrom] = at(enter);
    const auto [to, alphaTo] = at(leave);
    return DebugLine{
        .from = from, .to = to, .color = line.color, .alphaFrom = alphaFrom, .alphaTo = alphaTo};
}

std::size_t drawDebugLines(nvrhi::ICommandList& commandList, const DebugLinesPass& pass,
                           nvrhi::IFramebuffer& target, const glm::mat4& viewProjection,
                           std::span<const DebugLine> lines, DebugDepth depth, DebugBlend blend)
{
    if (lines.empty())
    {
        return 0;
    }
    const Frustum frustum = frustumOf(viewProjection);
    const bool alpha = blend == DebugBlend::Alpha;
    std::vector<DebugVertex> vertices;
    vertices.reserve(std::min<std::size_t>(lines.size(), MaxDebugLines) * 2);
    for (const DebugLine& source : lines)
    {
        const std::optional<DebugLine> line = clipToFrustum(source, frustum);
        if (!line)
        {
            continue;
        }
        if (vertices.size() >= 2 * static_cast<std::size_t>(MaxDebugLines))
        {
            warnOnceTooManyLines(lines.size());
            break;
        }
        vertices.push_back(
            {.position = line->from, .color = {line->color, alpha ? line->alphaFrom : 1.0f}});
        vertices.push_back(
            {.position = line->to, .color = {line->color, alpha ? line->alphaTo : 1.0f}});
    }
    if (vertices.empty())
    {
        return 0;
    }
    // writeBuffer se place dans la command list, avant le dessin : NVRHI met la barrière. Il coupe
    // la render pass sur Vulkan, et copie deux fois les lignes : acceptable pour du debug.
    commandList.writeBuffer(pass.vertices, vertices.data(), vertices.size() * sizeof(DebugVertex));
    commandList.writeBuffer(pass.constants, &viewProjection, sizeof(viewProjection));
    nvrhi::GraphicsState state;
    const bool onTop = depth == DebugDepth::OnTop;
    state.pipeline = alpha ? (onTop ? pass.blendedOnTopPipeline : pass.blendedPipeline)
                           : (onTop ? pass.onTopPipeline : pass.pipeline);
    state.framebuffer = &target;
    state.viewport.addViewportAndScissorRect(target.getFramebufferInfo().getViewport());
    state.addBindingSet(pass.bindings);
    state.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(pass.vertices).setSlot(0));
    commandList.setGraphicsState(state);
    nvrhi::DrawArguments arguments;
    arguments.vertexCount = static_cast<std::uint32_t>(vertices.size());
    commandList.draw(arguments);
    return vertices.size() / 2;
}

} // namespace levain::render
