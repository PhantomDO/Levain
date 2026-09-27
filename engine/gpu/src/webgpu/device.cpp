#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <utility>

#include "backend.hpp"
#include "conversions.hpp"

namespace levain::gpu::webgpu
{

namespace
{

/// Un multiple de `alignment`, qui est une puissance de deux.
std::uint64_t alignUp(std::uint64_t value, std::uint64_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

/// L'écart minimal entre deux versions d'un buffer de constantes volatil : celui qu'exige WebGPU
/// pour un offset dynamique (`minUniformBufferOffsetAlignment`, 256 par défaut et au plus).
constexpr std::uint64_t UniformOffsetAlignment = 256;

wgpu::StringView labelOf(const std::string& debugName)
{
    return {debugName.data(), debugName.size()};
}

} // namespace

Device::Device(wgpu::Instance instance, wgpu::Adapter adapter, wgpu::Device device,
               nvrhi::IMessageCallback* messageCallback)
    : instance{std::move(instance)}, adapter{std::move(adapter)}, device{std::move(device)},
      queue{this->device.GetQueue()}, m_messageCallback{messageCallback}
{
}

void Device::error(const std::string& message) const
{
    if (m_messageCallback != nullptr)
    {
        m_messageCallback->message(nvrhi::MessageSeverity::Error, message.c_str());
    }
}

nvrhi::BufferHandle Device::createBuffer(const nvrhi::BufferDesc& desc)
{
    // Un buffer volatil garde toutes ses versions d'une command list côte à côte, chacune alignée
    // comme l'exige un offset dynamique (ADR-0023, point 1). WebGPU veut une taille multiple de 4.
    const std::uint64_t size = desc.isVolatile ? alignUp(desc.byteSize, UniformOffsetAlignment) *
                                                     std::max<std::uint64_t>(desc.maxVersions, 1)
                                               : alignUp(desc.byteSize, 4);
    wgpu::BufferDescriptor wgpuDesc{};
    wgpuDesc.label = labelOf(desc.debugName);
    wgpuDesc.size = size;
    wgpuDesc.usage = bufferUsageOf(desc);
    wgpu::Buffer buffer = device.CreateBuffer(&wgpuDesc);
    if (!buffer)
    {
        error(std::format("buffer « {} » refusé par WebGPU", desc.debugName));
        return nullptr;
    }
    return nvrhi::BufferHandle::Create(new Buffer{desc, std::move(buffer)});
}

nvrhi::TextureHandle Device::createTexture(const nvrhi::TextureDesc& desc)
{
    const auto format = textureFormatOf(desc.format);
    if (!format)
    {
        error(std::format("texture « {} » : format {} sans équivalent WebGPU", desc.debugName,
                          nvrhi::getFormatInfo(desc.format).name));
        return nullptr;
    }
    const bool volume = desc.dimension == nvrhi::TextureDimension::Texture3D;
    wgpu::TextureDescriptor wgpuDesc{};
    wgpuDesc.label = labelOf(desc.debugName);
    wgpuDesc.size = {desc.width, desc.height, volume ? desc.depth : desc.arraySize};
    wgpuDesc.mipLevelCount = desc.mipLevels;
    wgpuDesc.sampleCount = desc.sampleCount;
    wgpuDesc.dimension = textureDimensionOf(desc.dimension);
    wgpuDesc.format = *format;
    wgpuDesc.usage = textureUsageOf(desc);
    wgpu::Texture texture = device.CreateTexture(&wgpuDesc);
    if (!texture)
    {
        error(std::format("texture « {} » refusée par WebGPU", desc.debugName));
        return nullptr;
    }
    return nvrhi::TextureHandle::Create(new Texture{desc, std::move(texture)});
}

nvrhi::SamplerHandle Device::createSampler(const nvrhi::SamplerDesc& desc)
{
    // WebGPU n'accepte l'anisotropie qu'avec des filtres tous linéaires, et au plus 16.
    const bool linear = desc.minFilter && desc.magFilter && desc.mipFilter;
    wgpu::SamplerDescriptor wgpuDesc{};
    wgpuDesc.addressModeU = addressModeOf(desc.addressU);
    wgpuDesc.addressModeV = addressModeOf(desc.addressV);
    wgpuDesc.addressModeW = addressModeOf(desc.addressW);
    wgpuDesc.magFilter = desc.magFilter ? wgpu::FilterMode::Linear : wgpu::FilterMode::Nearest;
    wgpuDesc.minFilter = desc.minFilter ? wgpu::FilterMode::Linear : wgpu::FilterMode::Nearest;
    wgpuDesc.mipmapFilter =
        desc.mipFilter ? wgpu::MipmapFilterMode::Linear : wgpu::MipmapFilterMode::Nearest;
    wgpuDesc.maxAnisotropy =
        linear ? static_cast<std::uint16_t>(std::clamp(desc.maxAnisotropy, 1.0f, 16.0f)) : 1;
    if (desc.reductionType == nvrhi::SamplerReductionType::Comparison)
    {
        wgpuDesc.compare = wgpu::CompareFunction::Less; // comme le backend Vulkan de NVRHI
    }
    wgpu::Sampler sampler = device.CreateSampler(&wgpuDesc);
    if (!sampler)
    {
        error("sampler refusé par WebGPU");
        return nullptr;
    }
    return nvrhi::SamplerHandle::Create(new Sampler{desc, std::move(sampler)});
}

bool Device::queryFeatureSupport(nvrhi::Feature, void*, size_t)
{
    // Aucune des fonctionnalités optionnelles de NVRHI (ray tracing, meshlets, VRS, files de
    // calcul séparées…) n'existe en WebGPU : le moteur choisit ses techniques de repli (ADR-0023).
    return false;
}

nvrhi::FormatSupport Device::queryFormatSupport(nvrhi::Format format)
{
    using S = nvrhi::FormatSupport;
    S support = S::None;
    if (vertexFormatOf(format))
    {
        support = support | S::VertexBuffer;
    }
    if (format == nvrhi::Format::R16_UINT || format == nvrhi::Format::R32_UINT)
    {
        support = support | S::IndexBuffer;
    }
    const auto textureFormat = textureFormatOf(format);
    if (!textureFormat)
    {
        return support;
    }
    const nvrhi::FormatInfo& info = nvrhi::getFormatInfo(format);
    if (info.kind == nvrhi::FormatKind::Integer && format == nvrhi::Format::R32_UINT)
    {
        return support | S::Texture | S::ShaderLoad;
    }
    if (info.hasDepth)
    {
        return support | S::Texture | S::DepthStencil | S::ShaderLoad;
    }
    // Les formats compressés ne s'échantillonnent que si le device a la fonctionnalité voulue.
    if (info.blockSize > 1)
    {
        return device.HasFeature(wgpu::FeatureName::TextureCompressionBC)
                   ? support | S::Texture | S::ShaderSample | S::ShaderLoad
                   : support;
    }
    return support | S::Texture | S::ShaderSample | S::ShaderLoad | S::RenderTarget | S::Blendable;
}

bool Device::waitForIdle()
{
#ifndef __EMSCRIPTEN__
    instance.WaitAny(queue.OnSubmittedWorkDone(wgpu::CallbackMode::WaitAnyOnly,
                                               [](wgpu::QueueWorkDoneStatus, wgpu::StringView) {}),
                     UINT64_MAX);
#endif
    // Le navigateur ne permet pas d'attendre le GPU : le travail soumis se termine de lui-même,
    // et WebGPU garde les ressources vivantes tant qu'il s'en sert.
    return true;
}

} // namespace levain::gpu::webgpu
