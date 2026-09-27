#pragma once

// Les correspondances entre les descriptions de NVRHI et celles de WebGPU.

#include <optional>

#include <nvrhi/nvrhi.h>
#include <webgpu/webgpu_cpp.h>

namespace levain::gpu::webgpu
{

/// Le format de texture WebGPU d'un format NVRHI, s'il en a un. WebGPU n'a ni les formats à trois
/// canaux ni la plupart des formats 16 bits normalisés.
[[nodiscard]] std::optional<wgpu::TextureFormat> textureFormatOf(nvrhi::Format format);

/// Le format d'attribut de sommet WebGPU d'un format NVRHI, s'il en a un.
[[nodiscard]] std::optional<wgpu::VertexFormat> vertexFormatOf(nvrhi::Format format);

[[nodiscard]] wgpu::BufferUsage bufferUsageOf(const nvrhi::BufferDesc& desc);
[[nodiscard]] wgpu::TextureUsage textureUsageOf(const nvrhi::TextureDesc& desc);
[[nodiscard]] wgpu::TextureDimension textureDimensionOf(nvrhi::TextureDimension dimension);
[[nodiscard]] wgpu::AddressMode addressModeOf(nvrhi::SamplerAddressMode mode);
[[nodiscard]] wgpu::CompareFunction compareFunctionOf(nvrhi::ComparisonFunc func);

} // namespace levain::gpu::webgpu
