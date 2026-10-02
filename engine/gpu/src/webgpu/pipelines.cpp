// Les pipelines de NVRHI en pipelines WebGPU. WebGPU décrit dans le pipeline ce que NVRHI met dans
// l'input layout (les sommets) et dans le framebuffer (les formats) : on les y recopie.

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <map>
#include <regex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "backend.hpp"
#include "conversions.hpp"

namespace levain::gpu::webgpu
{

namespace
{

wgpu::BlendFactor blendFactorOf(nvrhi::BlendFactor factor)
{
    using B = nvrhi::BlendFactor;
    using W = wgpu::BlendFactor;
    switch (factor)
    {
    case B::Zero:
        return W::Zero;
    case B::One:
        return W::One;
    case B::SrcColor:
        return W::Src;
    case B::InvSrcColor:
        return W::OneMinusSrc;
    case B::SrcAlpha:
        return W::SrcAlpha;
    case B::InvSrcAlpha:
        return W::OneMinusSrcAlpha;
    case B::DstAlpha:
        return W::DstAlpha;
    case B::InvDstAlpha:
        return W::OneMinusDstAlpha;
    case B::DstColor:
        return W::Dst;
    case B::InvDstColor:
        return W::OneMinusDst;
    case B::SrcAlphaSaturate:
        return W::SrcAlphaSaturated;
    case B::ConstantColor:
        return W::Constant;
    case B::InvConstantColor:
        return W::OneMinusConstant;
    default:
        return W::One; // les facteurs à deux sources n'existent pas en WebGPU de base
    }
}

wgpu::BlendOperation blendOperationOf(nvrhi::BlendOp op)
{
    switch (op)
    {
    case nvrhi::BlendOp::Subtract:
        return wgpu::BlendOperation::Subtract;
    case nvrhi::BlendOp::ReverseSubtract:
        return wgpu::BlendOperation::ReverseSubtract;
    case nvrhi::BlendOp::Min:
        return wgpu::BlendOperation::Min;
    case nvrhi::BlendOp::Max:
        return wgpu::BlendOperation::Max;
    default:
        return wgpu::BlendOperation::Add;
    }
}

wgpu::PrimitiveTopology topologyOf(nvrhi::PrimitiveType type)
{
    switch (type)
    {
    case nvrhi::PrimitiveType::PointList:
        return wgpu::PrimitiveTopology::PointList;
    case nvrhi::PrimitiveType::LineList:
        return wgpu::PrimitiveTopology::LineList;
    case nvrhi::PrimitiveType::LineStrip:
        return wgpu::PrimitiveTopology::LineStrip;
    case nvrhi::PrimitiveType::TriangleStrip:
        return wgpu::PrimitiveTopology::TriangleStrip;
    default:
        return wgpu::PrimitiveTopology::TriangleList;
    }
}

wgpu::CullMode cullModeOf(nvrhi::RasterCullMode mode)
{
    switch (mode)
    {
    case nvrhi::RasterCullMode::Front:
        return wgpu::CullMode::Front;
    case nvrhi::RasterCullMode::None:
        return wgpu::CullMode::None;
    default:
        return wgpu::CullMode::Back;
    }
}

wgpu::StringView viewOf(const std::string& text)
{
    return {text.data(), text.size()};
}

} // namespace

PipelineGroups Device::groupsOf(const nvrhi::BindingLayoutVector& layouts) const
{
    // Sous Vulkan, NVRHI range un layout au numéro de son espace de registres
    // (`registerSpaceIsDescriptorSet`), sinon à sa place dans la liste : même règle ici.
    PipelineGroups groups;
    for (std::size_t i = 0; i < layouts.size(); ++i)
    {
        const nvrhi::BindingLayoutDesc* desc = layouts[i]->getDesc();
        const auto group = desc->registerSpaceIsDescriptorSet ? desc->registerSpace
                                                              : static_cast<std::uint32_t>(i);
        groups.groupOf.push_back(group);
        groups.groupCount = std::max(groups.groupCount, group + 1);
    }
    return groups;
}

/// Les textures de profondeur et samplers de comparaison que déclarent les shaders, par groupe :
/// `@binding(b) @group(g) var nom : texture_depth_2d` (ou `sampler_comparison`), la forme qu'écrit
/// Slang en WGSL.
std::map<std::uint32_t, DepthBindings> depthBindingsOf(std::span<const Shader* const> shaders)
{
    static const std::regex declaration{
        R"(@binding\((\d+)\)\s*@group\((\d+)\)\s*var\s+\w+\s*:\s*(texture_depth_2d|sampler_comparison)\b)"};
    std::map<std::uint32_t, DepthBindings> groups;
    for (const Shader* shader : shaders)
    {
        if (shader == nullptr)
        {
            continue;
        }
        for (auto match =
                 std::sregex_iterator{shader->wgsl.begin(), shader->wgsl.end(), declaration};
             match != std::sregex_iterator{}; ++match)
        {
            const DepthBinding binding{
                .binding = static_cast<std::uint32_t>(std::stoul((*match)[1].str())),
                .comparisonSampler = (*match)[3].str() == "sampler_comparison"};
            DepthBindings& group =
                groups[static_cast<std::uint32_t>(std::stoul((*match)[2].str()))];
            if (std::ranges::find(group, binding) == group.end())
            {
                group.push_back(binding);
            }
        }
    }
    for (auto& [group, bindings] : groups)
    {
        std::ranges::sort(bindings);
    }
    return groups;
}

wgpu::PipelineLayout Device::pipelineLayoutOf(const nvrhi::BindingLayoutVector& layouts,
                                              const PipelineGroups& groups,
                                              std::span<const Shader* const> shaders) const
{
    const std::map<std::uint32_t, DepthBindings> depth = depthBindingsOf(shaders);
    std::vector<wgpu::BindGroupLayout> bindGroupLayouts(groups.groupCount, emptyLayout);
    for (std::size_t i = 0; i < layouts.size(); ++i)
    {
        const std::uint32_t group = groups.groupOf[i];
        const auto found = depth.find(group);
        bindGroupLayouts[group] =
            static_cast<BindingLayout*>(layouts[i].Get())
                ->layoutFor(device, found != depth.end() ? found->second : DepthBindings{});
    }
    wgpu::PipelineLayoutDescriptor layoutDesc{};
    layoutDesc.bindGroupLayoutCount = bindGroupLayouts.size();
    layoutDesc.bindGroupLayouts = bindGroupLayouts.data();
    return device.CreatePipelineLayout(&layoutDesc);
}

nvrhi::GraphicsPipelineHandle
Device::createGraphicsPipeline(const nvrhi::GraphicsPipelineDesc& desc,
                               nvrhi::IFramebuffer* framebuffer)
{
    return createGraphicsPipeline(desc, framebuffer->getFramebufferInfo());
}

nvrhi::GraphicsPipelineHandle
Device::createGraphicsPipeline(const nvrhi::GraphicsPipelineDesc& desc,
                               nvrhi::FramebufferInfo const& info)
{
    const auto* vertexShader = static_cast<const Shader*>(desc.VS.Get());
    const auto* pixelShader = static_cast<const Shader*>(desc.PS.Get());
    if (vertexShader == nullptr)
    {
        error("pipeline graphique sans vertex shader");
        return nullptr;
    }

    // Les sommets : un layout par buffer, ses attributs aux locations 0, 1, 2… dans l'ordre de
    // l'input layout, comme le backend Vulkan de NVRHI et comme Slang les numérote.
    std::vector<std::vector<wgpu::VertexAttribute>> attributesOf;
    std::vector<wgpu::VertexBufferLayout> vertexBuffers;
    if (const auto* inputLayout = static_cast<const InputLayout*>(desc.inputLayout.Get()))
    {
        std::uint32_t bufferCount = 0;
        for (const nvrhi::VertexAttributeDesc& attribute : inputLayout->attributes)
        {
            bufferCount = std::max(bufferCount, attribute.bufferIndex + 1);
        }
        attributesOf.resize(bufferCount);
        vertexBuffers.resize(bufferCount);
        std::uint32_t location = 0;
        for (const nvrhi::VertexAttributeDesc& attribute : inputLayout->attributes)
        {
            // Vérifié par createInputLayout ; un input layout d'un autre device serait un bug.
            const auto format = vertexFormatOf(attribute.format);
            if (!format)
            {
                error("pipeline graphique : format de sommet sans équivalent WebGPU");
                return nullptr;
            }
            attributesOf[attribute.bufferIndex].push_back(
                {.format = *format, .offset = attribute.offset, .shaderLocation = location++});
            wgpu::VertexBufferLayout& buffer = vertexBuffers[attribute.bufferIndex];
            buffer.arrayStride = attribute.elementStride;
            buffer.stepMode = attribute.isInstanced ? wgpu::VertexStepMode::Instance
                                                    : wgpu::VertexStepMode::Vertex;
        }
        for (std::uint32_t i = 0; i < bufferCount; ++i)
        {
            vertexBuffers[i].attributeCount = attributesOf[i].size();
            vertexBuffers[i].attributes = attributesOf[i].data();
        }
    }

    // Les cibles : leurs formats viennent du framebuffer, le mélange de l'état de rendu.
    std::vector<wgpu::ColorTargetState> targets;
    std::vector<wgpu::BlendState> blends(info.colorFormats.size());
    for (std::size_t i = 0; i < info.colorFormats.size(); ++i)
    {
        const nvrhi::BlendState::RenderTarget& blend = desc.renderState.blendState.targets[i];
        const auto format = textureFormatOf(info.colorFormats[i]);
        if (!format)
        {
            error("pipeline graphique : format de cible sans équivalent WebGPU");
            return nullptr;
        }
        wgpu::ColorTargetState& target = targets.emplace_back();
        target.format = *format;
        target.writeMask = static_cast<wgpu::ColorWriteMask>(blend.colorWriteMask);
        if (blend.blendEnable)
        {
            blends[i].color = {.operation = blendOperationOf(blend.blendOp),
                               .srcFactor = blendFactorOf(blend.srcBlend),
                               .dstFactor = blendFactorOf(blend.destBlend)};
            blends[i].alpha = {.operation = blendOperationOf(blend.blendOpAlpha),
                               .srcFactor = blendFactorOf(blend.srcBlendAlpha),
                               .dstFactor = blendFactorOf(blend.destBlendAlpha)};
            target.blend = &blends[i];
        }
    }

    const nvrhi::DepthStencilState& depth = desc.renderState.depthStencilState;
    wgpu::DepthStencilState depthStencil{};
    if (info.depthFormat != nvrhi::Format::UNKNOWN)
    {
        const auto format = textureFormatOf(info.depthFormat);
        if (!format)
        {
            error("pipeline graphique : format de profondeur sans équivalent WebGPU");
            return nullptr;
        }
        depthStencil.format = *format;
        depthStencil.depthWriteEnabled =
            depth.depthWriteEnable ? wgpu::OptionalBool::True : wgpu::OptionalBool::False;
        depthStencil.depthCompare = depth.depthTestEnable ? compareFunctionOf(depth.depthFunc)
                                                          : wgpu::CompareFunction::Always;
    }

    const std::string vertexEntry{vertexShader->desc.entryName};
    const std::string pixelEntry = pixelShader != nullptr ? pixelShader->desc.entryName : "";
    wgpu::FragmentState fragment{};
    if (pixelShader != nullptr)
    {
        fragment.module = pixelShader->module;
        fragment.entryPoint = viewOf(pixelEntry);
        fragment.targetCount = targets.size();
        fragment.targets = targets.data();
    }

    const PipelineGroups groups = groupsOf(desc.bindingLayouts);
    wgpu::RenderPipelineDescriptor pipelineDesc{};
    const std::array<const Shader*, 2> shaders{vertexShader, pixelShader};
    pipelineDesc.layout = pipelineLayoutOf(desc.bindingLayouts, groups, shaders);
    pipelineDesc.vertex.module = vertexShader->module;
    pipelineDesc.vertex.entryPoint = viewOf(vertexEntry);
    pipelineDesc.vertex.bufferCount = vertexBuffers.size();
    pipelineDesc.vertex.buffers = vertexBuffers.data();
    pipelineDesc.fragment = pixelShader != nullptr ? &fragment : nullptr;
    pipelineDesc.depthStencil =
        info.depthFormat != nvrhi::Format::UNKNOWN ? &depthStencil : nullptr;
    pipelineDesc.primitive.topology = topologyOf(desc.primType);
    pipelineDesc.primitive.frontFace = desc.renderState.rasterState.frontCounterClockwise
                                           ? wgpu::FrontFace::CCW
                                           : wgpu::FrontFace::CW;
    pipelineDesc.primitive.cullMode = cullModeOf(desc.renderState.rasterState.cullMode);
    pipelineDesc.multisample.count = info.sampleCount;

    wgpu::RenderPipeline pipeline = device.CreateRenderPipeline(&pipelineDesc);
    if (!pipeline)
    {
        error("pipeline graphique refusé par WebGPU");
        return nullptr;
    }
    return nvrhi::GraphicsPipelineHandle::Create(
        new GraphicsPipeline{desc, info, std::move(pipeline), groups});
}

nvrhi::ComputePipelineHandle Device::createComputePipeline(const nvrhi::ComputePipelineDesc& desc)
{
    const auto* shader = static_cast<const Shader*>(desc.CS.Get());
    if (shader == nullptr)
    {
        error("pipeline de calcul sans compute shader");
        return nullptr;
    }
    const std::string entry{shader->desc.entryName};
    const PipelineGroups groups = groupsOf(desc.bindingLayouts);
    wgpu::ComputePipelineDescriptor pipelineDesc{};
    const std::array<const Shader*, 1> shaders{shader};
    pipelineDesc.layout = pipelineLayoutOf(desc.bindingLayouts, groups, shaders);
    pipelineDesc.compute.module = shader->module;
    pipelineDesc.compute.entryPoint = viewOf(entry);
    wgpu::ComputePipeline pipeline = device.CreateComputePipeline(&pipelineDesc);
    if (!pipeline)
    {
        error("pipeline de calcul refusé par WebGPU");
        return nullptr;
    }
    return nvrhi::ComputePipelineHandle::Create(
        new ComputePipeline{desc, std::move(pipeline), groups});
}

} // namespace levain::gpu::webgpu
