// Les binding layouts et binding sets de NVRHI en bind group layouts et bind groups de WebGPU.
//
// Le numéro de binding d'une ressource est celui qu'elle a sous Vulkan : son slot, plus le décalage
// de son type (`VulkanBindingOffsets`), que shaders/CMakeLists.txt applique aussi au WGSL. Le
// numéro de groupe est l'espace de registres (`registerSpaceIsDescriptorSet`, ADR-0013).

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include "backend.hpp"
#include "conversions.hpp"

namespace levain::gpu::webgpu
{

namespace
{

/// Le binding WGSL d'une ressource : son slot, décalé selon son type, comme sous Vulkan.
std::uint32_t bindingOf(nvrhi::ResourceType type, std::uint32_t slot,
                        const nvrhi::VulkanBindingOffsets& offsets)
{
    switch (type)
    {
    case nvrhi::ResourceType::Sampler:
        return slot + offsets.sampler;
    case nvrhi::ResourceType::ConstantBuffer:
    case nvrhi::ResourceType::VolatileConstantBuffer:
        return slot + offsets.constantBuffer;
    case nvrhi::ResourceType::Texture_UAV:
    case nvrhi::ResourceType::TypedBuffer_UAV:
    case nvrhi::ResourceType::StructuredBuffer_UAV:
    case nvrhi::ResourceType::RawBuffer_UAV:
        return slot + offsets.unorderedAccess;
    default:
        return slot + offsets.shaderResource;
    }
}

bool isWritableBuffer(nvrhi::ResourceType type)
{
    return type == nvrhi::ResourceType::StructuredBuffer_UAV ||
           type == nvrhi::ResourceType::RawBuffer_UAV ||
           type == nvrhi::ResourceType::TypedBuffer_UAV;
}

/// Les étapes de shader qui voient la ressource. WebGPU interdit un storage buffer en écriture à
/// l'étape des sommets : il n'y est pas visible, même si le layout dit « toutes ».
wgpu::ShaderStage stagesOf(nvrhi::ShaderType visibility, bool writable)
{
    wgpu::ShaderStage stages = wgpu::ShaderStage::None;
    if ((visibility & nvrhi::ShaderType::Vertex) != nvrhi::ShaderType::None && !writable)
    {
        stages |= wgpu::ShaderStage::Vertex;
    }
    if ((visibility & nvrhi::ShaderType::Pixel) != nvrhi::ShaderType::None)
    {
        stages |= wgpu::ShaderStage::Fragment;
    }
    if ((visibility & nvrhi::ShaderType::Compute) != nvrhi::ShaderType::None)
    {
        stages |= wgpu::ShaderStage::Compute;
    }
    return stages;
}

} // namespace

nvrhi::BindingLayoutHandle Device::createBindingLayout(const nvrhi::BindingLayoutDesc& desc)
{
    std::vector<wgpu::BindGroupLayoutEntry> entries;
    for (const nvrhi::BindingLayoutItem& item : desc.bindings)
    {
        if (item.size > 1)
        {
            error("binding layout : les tableaux de ressources ne sont pas pris en charge");
            return nullptr;
        }
        wgpu::BindGroupLayoutEntry entry{};
        entry.binding = bindingOf(item.type, item.slot, desc.bindingOffsets);
        entry.visibility = stagesOf(desc.visibility, isWritableBuffer(item.type));
        switch (item.type)
        {
        case nvrhi::ResourceType::Texture_SRV:
            // Une texture 2D filtrable, sauf indication contraire (`layoutFor`) : profondeur, cube.
            entry.texture.sampleType = wgpu::TextureSampleType::Float;
            entry.texture.viewDimension = wgpu::TextureViewDimension::e2D;
            break;
        case nvrhi::ResourceType::Sampler:
            entry.sampler.type = wgpu::SamplerBindingType::Filtering;
            break;
        case nvrhi::ResourceType::Texture_UAV:
            // Écrite par un compute. Son format et sa dimension viennent des indications
            // (`layoutFor`) ; ce format de remplacement garde valide le layout sans elles.
            entry.storageTexture.access = wgpu::StorageTextureAccess::WriteOnly;
            entry.storageTexture.format = wgpu::TextureFormat::RGBA8Unorm;
            entry.storageTexture.viewDimension = wgpu::TextureViewDimension::e2D;
            break;
        case nvrhi::ResourceType::ConstantBuffer:
            entry.buffer.type = wgpu::BufferBindingType::Uniform;
            break;
        case nvrhi::ResourceType::VolatileConstantBuffer:
            // Chaque version a sa place dans le buffer ; la command list choisit la sienne par un
            // offset dynamique, à chaque dessin (ADR-0023, point 1).
            entry.buffer.type = wgpu::BufferBindingType::Uniform;
            entry.buffer.hasDynamicOffset = true;
            break;
        case nvrhi::ResourceType::StructuredBuffer_SRV:
        case nvrhi::ResourceType::RawBuffer_SRV:
        case nvrhi::ResourceType::TypedBuffer_SRV:
            entry.buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
            break;
        case nvrhi::ResourceType::StructuredBuffer_UAV:
        case nvrhi::ResourceType::RawBuffer_UAV:
        case nvrhi::ResourceType::TypedBuffer_UAV:
            entry.buffer.type = wgpu::BufferBindingType::Storage;
            break;
        default:
            error(std::format("binding layout : ressource de type {} non prise en charge",
                              static_cast<int>(item.type)));
            return nullptr;
        }
        entries.push_back(entry);
    }
    wgpu::BindGroupLayoutDescriptor layoutDesc{};
    layoutDesc.entryCount = entries.size();
    layoutDesc.entries = entries.data();
    wgpu::BindGroupLayout layout = device.CreateBindGroupLayout(&layoutDesc);
    if (!layout)
    {
        error("binding layout refusé par WebGPU");
        return nullptr;
    }
    return nvrhi::BindingLayoutHandle::Create(
        new BindingLayout{desc, std::move(layout), std::move(entries)});
}

wgpu::BindGroupLayout BindingLayout::layoutFor(const wgpu::Device& device,
                                               const BindingHints& hints) const
{
    if (hints.empty())
    {
        return layout;
    }
    if (auto found = variants.find(hints); found != variants.end())
    {
        return found->second;
    }
    std::vector<wgpu::BindGroupLayoutEntry> changed = entries;
    for (wgpu::BindGroupLayoutEntry& entry : changed)
    {
        const auto hint = std::ranges::find(hints, entry.binding, &BindingHint::binding);
        if (hint == hints.end())
        {
            continue;
        }
        switch (hint->kind)
        {
        case BindingHint::Kind::DepthTexture:
            entry.texture.sampleType = wgpu::TextureSampleType::Depth;
            break;
        case BindingHint::Kind::ComparisonSampler:
            entry.sampler.type = wgpu::SamplerBindingType::Comparison;
            break;
        case BindingHint::Kind::CubeTexture:
            entry.texture.viewDimension = wgpu::TextureViewDimension::Cube;
            break;
        case BindingHint::Kind::StorageTexture2D:
        case BindingHint::Kind::StorageTexture2DArray:
            entry.storageTexture.format = hint->format;
            entry.storageTexture.viewDimension = hint->kind == BindingHint::Kind::StorageTexture2D
                                                     ? wgpu::TextureViewDimension::e2D
                                                     : wgpu::TextureViewDimension::e2DArray;
            break;
        }
    }
    wgpu::BindGroupLayoutDescriptor layoutDesc{};
    layoutDesc.entryCount = changed.size();
    layoutDesc.entries = changed.data();
    return variants.emplace(hints, device.CreateBindGroupLayout(&layoutDesc)).first->second;
}

nvrhi::BindingSetHandle Device::createBindingSet(const nvrhi::BindingSetDesc& desc,
                                                 nvrhi::IBindingLayout* layout)
{
    const auto* bindingLayout = static_cast<const BindingLayout*>(layout);
    std::vector<wgpu::BindGroupEntry> entries;
    std::vector<std::pair<std::uint32_t, nvrhi::BufferHandle>> volatileBuffers;
    // Ce que les ressources liées disent de leur binding (profondeur, cube, storage texture) : leur
    // layout le dira à WebGPU.
    BindingHints hints;
    for (const nvrhi::BindingSetItem& item : desc.bindings)
    {
        wgpu::BindGroupEntry entry{};
        entry.binding = bindingOf(item.type, item.slot, bindingLayout->desc.bindingOffsets);
        switch (item.type)
        {
        case nvrhi::ResourceType::Texture_SRV:
        {
            auto* texture = static_cast<Texture*>(item.resourceHandle);
            const nvrhi::TextureSubresourceSet subresources =
                item.subresources.resolve(texture->desc, false);
            const bool cube = texture->desc.dimension == nvrhi::TextureDimension::TextureCube;
            wgpu::TextureViewDescriptor viewDesc{};
            viewDesc.dimension =
                cube ? wgpu::TextureViewDimension::Cube : wgpu::TextureViewDimension::e2D;
            viewDesc.baseMipLevel = subresources.baseMipLevel;
            viewDesc.mipLevelCount = subresources.numMipLevels;
            viewDesc.baseArrayLayer = subresources.baseArraySlice;
            viewDesc.arrayLayerCount = subresources.numArraySlices;
            entry.textureView = texture->texture.CreateView(&viewDesc);
            if (nvrhi::getFormatInfo(texture->desc.format).hasDepth)
            {
                hints.push_back(
                    {.binding = entry.binding, .kind = BindingHint::Kind::DepthTexture});
            }
            if (cube)
            {
                hints.push_back({.binding = entry.binding, .kind = BindingHint::Kind::CubeTexture});
            }
            break;
        }
        case nvrhi::ResourceType::Texture_UAV:
        {
            // Un seul niveau de mip ; une cubemap ou un tableau, toutes ses couches en 2D.
            auto* texture = static_cast<Texture*>(item.resourceHandle);
            const nvrhi::TextureSubresourceSet subresources =
                item.subresources.resolve(texture->desc, true);
            const bool layered = texture->desc.arraySize > 1;
            const nvrhi::Format format =
                item.format != nvrhi::Format::UNKNOWN ? item.format : texture->desc.format;
            const auto wgpuFormat = textureFormatOf(format);
            if (!wgpuFormat)
            {
                error("binding set : format de storage texture sans équivalent WebGPU");
                return nullptr;
            }
            wgpu::TextureViewDescriptor viewDesc{};
            viewDesc.dimension =
                layered ? wgpu::TextureViewDimension::e2DArray : wgpu::TextureViewDimension::e2D;
            viewDesc.format = *wgpuFormat;
            viewDesc.baseMipLevel = subresources.baseMipLevel;
            viewDesc.mipLevelCount = 1;
            viewDesc.baseArrayLayer = subresources.baseArraySlice;
            viewDesc.arrayLayerCount = subresources.numArraySlices;
            entry.textureView = texture->texture.CreateView(&viewDesc);
            hints.push_back({.binding = entry.binding,
                             .kind = layered ? BindingHint::Kind::StorageTexture2DArray
                                             : BindingHint::Kind::StorageTexture2D,
                             .format = *wgpuFormat});
            break;
        }
        case nvrhi::ResourceType::Sampler:
        {
            const auto* sampler = static_cast<Sampler*>(item.resourceHandle);
            entry.sampler = sampler->sampler;
            if (sampler->desc.reductionType == nvrhi::SamplerReductionType::Comparison)
            {
                hints.push_back(
                    {.binding = entry.binding, .kind = BindingHint::Kind::ComparisonSampler});
            }
            break;
        }
        case nvrhi::ResourceType::VolatileConstantBuffer:
        {
            // Une version seulement : l'offset dynamique la choisit dans le buffer.
            auto* buffer = static_cast<Buffer*>(item.resourceHandle);
            entry.buffer = buffer->buffer;
            entry.offset = 0;
            entry.size = buffer->desc.byteSize;
            volatileBuffers.emplace_back(entry.binding, buffer);
            break;
        }
        default:
        {
            auto* buffer = static_cast<Buffer*>(item.resourceHandle);
            const nvrhi::BufferRange range = item.range.resolve(buffer->desc);
            entry.buffer = buffer->buffer;
            entry.offset = range.byteOffset;
            entry.size = range.byteSize;
            break;
        }
        }
        entries.push_back(entry);
    }
    wgpu::BindGroupDescriptor groupDesc{};
    std::ranges::sort(hints);
    groupDesc.layout = bindingLayout->layoutFor(device, hints);
    groupDesc.entryCount = entries.size();
    groupDesc.entries = entries.data();
    wgpu::BindGroup group = device.CreateBindGroup(&groupDesc);
    if (!group)
    {
        error("binding set refusé par WebGPU");
        return nullptr;
    }
    // WebGPU attend les offsets dynamiques dans l'ordre croissant des numéros de binding.
    std::ranges::sort(volatileBuffers, {}, &std::pair<std::uint32_t, nvrhi::BufferHandle>::first);
    std::vector<nvrhi::BufferHandle> ordered;
    ordered.reserve(volatileBuffers.size());
    for (auto& [binding, buffer] : volatileBuffers)
    {
        ordered.push_back(std::move(buffer));
    }
    return nvrhi::BindingSetHandle::Create(
        new BindingSet{desc, layout, std::move(group), std::move(ordered)});
}

} // namespace levain::gpu::webgpu
