#include "conversions.hpp"

namespace levain::gpu::webgpu
{

std::optional<wgpu::TextureFormat> textureFormatOf(nvrhi::Format format)
{
    using F = nvrhi::Format;
    using W = wgpu::TextureFormat;
    switch (format)
    {
    case F::R8_UNORM:
        return W::R8Unorm;
    case F::RG8_UNORM:
        return W::RG8Unorm;
    case F::RGBA8_UNORM:
        return W::RGBA8Unorm;
    case F::SRGBA8_UNORM:
        return W::RGBA8UnormSrgb;
    case F::BGRA8_UNORM:
        return W::BGRA8Unorm;
    case F::SBGRA8_UNORM:
        return W::BGRA8UnormSrgb;
    case F::R16_FLOAT:
        return W::R16Float;
    case F::RG16_FLOAT:
        return W::RG16Float;
    case F::RGBA16_FLOAT:
        return W::RGBA16Float;
    case F::R32_FLOAT:
        return W::R32Float;
    case F::RG32_FLOAT:
        return W::RG32Float;
    case F::RGBA32_FLOAT:
        return W::RGBA32Float;
    case F::R32_UINT:
        return W::R32Uint;
    case F::R11G11B10_FLOAT:
        return W::RG11B10Ufloat;
    case F::D16:
        return W::Depth16Unorm;
    case F::D24S8:
        return W::Depth24PlusStencil8;
    case F::D32:
        return W::Depth32Float;
    case F::D32S8:
        return W::Depth32FloatStencil8;
    case F::BC7_UNORM:
        return W::BC7RGBAUnorm;
    case F::BC7_UNORM_SRGB:
        return W::BC7RGBAUnormSrgb;
    default:
        return std::nullopt;
    }
}

std::optional<wgpu::VertexFormat> vertexFormatOf(nvrhi::Format format)
{
    using F = nvrhi::Format;
    using W = wgpu::VertexFormat;
    switch (format)
    {
    case F::R32_FLOAT:
        return W::Float32;
    case F::RG32_FLOAT:
        return W::Float32x2;
    case F::RGB32_FLOAT:
        return W::Float32x3;
    case F::RGBA32_FLOAT:
        return W::Float32x4;
    case F::R32_UINT:
        return W::Uint32;
    case F::RG32_UINT:
        return W::Uint32x2;
    case F::RGB32_UINT:
        return W::Uint32x3;
    case F::RGBA32_UINT:
        return W::Uint32x4;
    case F::RGBA8_UNORM:
        return W::Unorm8x4;
    case F::RGBA16_UINT:
        return W::Uint16x4;
    default:
        return std::nullopt;
    }
}

wgpu::BufferUsage bufferUsageOf(const nvrhi::BufferDesc& desc)
{
    // Un buffer relu par le CPU n'a droit, en WebGPU, qu'à MapRead et CopyDst : il reçoit une
    // copie, comme un staging buffer de NVRHI.
    if (desc.cpuAccess == nvrhi::CpuAccessMode::Read)
    {
        return wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst;
    }
    wgpu::BufferUsage usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc;
    if (desc.isVertexBuffer)
    {
        usage |= wgpu::BufferUsage::Vertex;
    }
    if (desc.isIndexBuffer)
    {
        usage |= wgpu::BufferUsage::Index;
    }
    if (desc.isConstantBuffer)
    {
        usage |= wgpu::BufferUsage::Uniform;
    }
    if (desc.isDrawIndirectArgs)
    {
        usage |= wgpu::BufferUsage::Indirect;
    }
    // Structuré, brut ou en écriture depuis un shader : un storage buffer.
    if (desc.structStride != 0 || desc.canHaveRawViews || desc.canHaveUAVs)
    {
        usage |= wgpu::BufferUsage::Storage;
    }
    return usage;
}

wgpu::TextureUsage textureUsageOf(const nvrhi::TextureDesc& desc)
{
    wgpu::TextureUsage usage = wgpu::TextureUsage::CopyDst | wgpu::TextureUsage::CopySrc;
    if (desc.isShaderResource)
    {
        usage |= wgpu::TextureUsage::TextureBinding;
    }
    if (desc.isRenderTarget)
    {
        usage |= wgpu::TextureUsage::RenderAttachment;
    }
    if (desc.isUAV)
    {
        usage |= wgpu::TextureUsage::StorageBinding;
    }
    return usage;
}

wgpu::TextureDimension textureDimensionOf(nvrhi::TextureDimension dimension)
{
    switch (dimension)
    {
    case nvrhi::TextureDimension::Texture1D:
    case nvrhi::TextureDimension::Texture1DArray:
        return wgpu::TextureDimension::e1D;
    case nvrhi::TextureDimension::Texture3D:
        return wgpu::TextureDimension::e3D;
    default:
        return wgpu::TextureDimension::e2D; // 2D, tableaux 2D et cubes
    }
}

wgpu::AddressMode addressModeOf(nvrhi::SamplerAddressMode mode)
{
    switch (mode)
    {
    case nvrhi::SamplerAddressMode::Wrap:
        return wgpu::AddressMode::Repeat;
    case nvrhi::SamplerAddressMode::Mirror:
        return wgpu::AddressMode::MirrorRepeat;
    // WebGPU n'a pas de couleur de bord : Border se replie sur Clamp.
    default:
        return wgpu::AddressMode::ClampToEdge;
    }
}

wgpu::CompareFunction compareFunctionOf(nvrhi::ComparisonFunc func)
{
    using C = nvrhi::ComparisonFunc;
    using W = wgpu::CompareFunction;
    switch (func)
    {
    case C::Never:
        return W::Never;
    case C::Less:
        return W::Less;
    case C::Equal:
        return W::Equal;
    case C::LessOrEqual:
        return W::LessEqual;
    case C::Greater:
        return W::Greater;
    case C::NotEqual:
        return W::NotEqual;
    case C::GreaterOrEqual:
        return W::GreaterEqual;
    case C::Always:
        return W::Always;
    }
    return W::Always;
}

} // namespace levain::gpu::webgpu
