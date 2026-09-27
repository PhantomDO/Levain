#include "commandlist.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <format>
#include <vector>

#include "conversions.hpp"

namespace levain::gpu::webgpu
{

namespace
{

constexpr std::uint64_t UniformOffsetAlignment = 256; ///< Comme dans device.cpp.

std::uint64_t alignUp(std::uint64_t value, std::uint64_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

/// La vue d'un attachement de framebuffer : son format, son niveau de mip et sa tranche.
wgpu::TextureView attachmentView(const nvrhi::FramebufferAttachment& attachment)
{
    auto* texture = static_cast<Texture*>(attachment.texture);
    const nvrhi::TextureSubresourceSet subresources =
        attachment.subresources.resolve(texture->desc, true);
    wgpu::TextureViewDescriptor viewDesc{};
    viewDesc.dimension = wgpu::TextureViewDimension::e2D;
    viewDesc.baseMipLevel = subresources.baseMipLevel;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = subresources.baseArraySlice;
    viewDesc.arrayLayerCount = 1;
    return texture->texture.CreateView(&viewDesc);
}

/// L'étendue d'une copie de texture : WebGPU la veut en blocs entiers pour un format compressé.
wgpu::Extent3D copyExtentOf(const nvrhi::TextureSlice& slice, nvrhi::Format format)
{
    const std::uint32_t block = nvrhi::getFormatInfo(format).blockSize;
    return {static_cast<std::uint32_t>(alignUp(slice.width, block)),
            static_cast<std::uint32_t>(alignUp(slice.height, block)), slice.depth};
}

} // namespace

void CommandList::unsupported(const char* what) const
{
    m_device.error(std::format("command list WebGPU : {} non pris en charge", what));
}

void CommandList::open()
{
    m_encoder = m_device.device.CreateCommandEncoder();
    commands = nullptr;
    m_nextVersion.clear();
    m_written.clear();
    clearState();
}

void CommandList::close()
{
    endPass();
    commands = m_encoder.Finish();
    m_encoder = nullptr;
}

void CommandList::clearState()
{
    endPass();
    m_graphics = {};
    m_compute = {};
}

void CommandList::beginPass(nvrhi::IFramebuffer* framebuffer)
{
    if (m_pass && m_passFramebuffer == framebuffer)
    {
        return;
    }
    endPass();
    const nvrhi::FramebufferDesc& desc = framebuffer->getDesc();
    std::vector<wgpu::RenderPassColorAttachment> colors;
    for (const nvrhi::FramebufferAttachment& attachment : desc.colorAttachments)
    {
        wgpu::RenderPassColorAttachment& color = colors.emplace_back();
        color.view = attachmentView(attachment);
        color.loadOp = wgpu::LoadOp::Load;
        color.storeOp = wgpu::StoreOp::Store;
    }
    wgpu::RenderPassDepthStencilAttachment depth{};
    wgpu::RenderPassDescriptor passDesc{};
    passDesc.colorAttachmentCount = colors.size();
    passDesc.colorAttachments = colors.data();
    if (desc.depthAttachment.valid())
    {
        depth.view = attachmentView(desc.depthAttachment);
        depth.depthLoadOp = wgpu::LoadOp::Load;
        depth.depthStoreOp = wgpu::StoreOp::Store;
        depth.depthReadOnly = desc.depthAttachment.isReadOnly;
        passDesc.depthStencilAttachment = &depth;
    }
    m_pass = m_encoder.BeginRenderPass(&passDesc);
    m_passFramebuffer = framebuffer;
}

void CommandList::endPass()
{
    if (m_pass)
    {
        m_pass.End();
        m_pass = nullptr;
        m_passFramebuffer = nullptr;
    }
}

void CommandList::clearTextureFloat(nvrhi::ITexture* texture,
                                    nvrhi::TextureSubresourceSet subresources,
                                    const nvrhi::Color& color)
{
    // WebGPU n'efface qu'en ouvrant une passe : une passe vide, qui efface et se referme.
    endPass();
    auto* target = static_cast<Texture*>(texture);
    const nvrhi::TextureSubresourceSet resolved = subresources.resolve(target->desc, false);
    for (nvrhi::MipLevel mip = resolved.baseMipLevel;
         mip < resolved.baseMipLevel + resolved.numMipLevels; ++mip)
    {
        wgpu::RenderPassColorAttachment attachment{};
        attachment.view =
            attachmentView(nvrhi::FramebufferAttachment().setTexture(texture).setSubresources(
                nvrhi::TextureSubresourceSet(mip, 1, resolved.baseArraySlice, 1)));
        attachment.loadOp = wgpu::LoadOp::Clear;
        attachment.storeOp = wgpu::StoreOp::Store;
        attachment.clearValue = {color.r, color.g, color.b, color.a};
        wgpu::RenderPassDescriptor passDesc{};
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &attachment;
        m_encoder.BeginRenderPass(&passDesc).End();
    }
}

void CommandList::clearDepthStencilTexture(nvrhi::ITexture* texture,
                                           nvrhi::TextureSubresourceSet subresources,
                                           bool clearDepth, float depth, bool clearStencil,
                                           uint8_t stencil)
{
    endPass();
    auto* target = static_cast<Texture*>(texture);
    const bool hasStencil = nvrhi::getFormatInfo(target->desc.format).hasStencil;
    wgpu::RenderPassDepthStencilAttachment attachment{};
    attachment.view = attachmentView(
        nvrhi::FramebufferAttachment().setTexture(texture).setSubresources(subresources));
    attachment.depthLoadOp = clearDepth ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
    attachment.depthStoreOp = wgpu::StoreOp::Store;
    attachment.depthClearValue = depth;
    if (hasStencil)
    {
        attachment.stencilLoadOp = clearStencil ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
        attachment.stencilStoreOp = wgpu::StoreOp::Store;
        attachment.stencilClearValue = stencil;
    }
    wgpu::RenderPassDescriptor passDesc{};
    passDesc.depthStencilAttachment = &attachment;
    m_encoder.BeginRenderPass(&passDesc).End();
}

void CommandList::copyTexture(nvrhi::ITexture* dest, const nvrhi::TextureSlice& destSlice,
                              nvrhi::ITexture* src, const nvrhi::TextureSlice& srcSlice)
{
    endPass();
    auto* destination = static_cast<Texture*>(dest);
    auto* source = static_cast<Texture*>(src);
    const nvrhi::TextureSlice from = srcSlice.resolve(source->desc);
    const nvrhi::TextureSlice to = destSlice.resolve(destination->desc);
    wgpu::TexelCopyTextureInfo sourceInfo{};
    sourceInfo.texture = source->texture;
    sourceInfo.mipLevel = from.mipLevel;
    sourceInfo.origin = {from.x, from.y, from.arraySlice};
    wgpu::TexelCopyTextureInfo destinationInfo{};
    destinationInfo.texture = destination->texture;
    destinationInfo.mipLevel = to.mipLevel;
    destinationInfo.origin = {to.x, to.y, to.arraySlice};
    const wgpu::Extent3D extent = copyExtentOf(from, source->desc.format);
    m_encoder.CopyTextureToTexture(&sourceInfo, &destinationInfo, &extent);
}

void CommandList::copyTexture(nvrhi::IStagingTexture* dest, const nvrhi::TextureSlice&,
                              nvrhi::ITexture* src, const nvrhi::TextureSlice& srcSlice)
{
    // ponytail: la texture de relecture ne garde qu'un niveau, le 0 : ce que font --capture et le
    // test de fumée. Une tranche par niveau de mip si un jour on relit des mips.
    endPass();
    auto* staging = static_cast<StagingTexture*>(dest);
    auto* source = static_cast<Texture*>(src);
    const nvrhi::TextureSlice from = srcSlice.resolve(source->desc);
    wgpu::TexelCopyTextureInfo sourceInfo{};
    sourceInfo.texture = source->texture;
    sourceInfo.mipLevel = from.mipLevel;
    sourceInfo.origin = {from.x, from.y, from.arraySlice};
    wgpu::TexelCopyBufferInfo destinationInfo{};
    destinationInfo.buffer = staging->buffer;
    destinationInfo.layout.bytesPerRow = staging->bytesPerRow;
    destinationInfo.layout.rowsPerImage = staging->desc.height;
    const wgpu::Extent3D extent = copyExtentOf(from, source->desc.format);
    m_encoder.CopyTextureToBuffer(&sourceInfo, &destinationInfo, &extent);
}

void CommandList::writeTexture(nvrhi::ITexture* dest, uint32_t arraySlice, uint32_t mipLevel,
                               const void* data, size_t rowPitch, size_t depthPitch)
{
    auto* texture = static_cast<Texture*>(dest);
    const nvrhi::TextureSlice slice = nvrhi::TextureSlice()
                                          .setMipLevel(mipLevel)
                                          .setArraySlice(arraySlice)
                                          .resolve(texture->desc);
    const wgpu::Extent3D extent = copyExtentOf(slice, texture->desc.format);
    const std::uint32_t block = nvrhi::getFormatInfo(texture->desc.format).blockSize;
    wgpu::TexelCopyTextureInfo destination{};
    destination.texture = texture->texture;
    destination.mipLevel = mipLevel;
    destination.origin = {0, 0, arraySlice};
    wgpu::TexelCopyBufferLayout layout{};
    layout.bytesPerRow = static_cast<std::uint32_t>(rowPitch);
    layout.rowsPerImage = extent.height / block;
    const std::size_t size = depthPitch != 0 ? depthPitch : rowPitch * layout.rowsPerImage;
    m_device.queue.WriteTexture(&destination, data, size, &layout, &extent);
}

void CommandList::writeBuffer(nvrhi::IBuffer* buffer, const void* data, size_t dataSize,
                              uint64_t destOffsetBytes)
{
    auto* target = static_cast<Buffer*>(buffer);
    std::uint64_t offset = destOffsetBytes;
    if (target->desc.isVolatile)
    {
        // Une version de plus : les dessins déjà enregistrés gardent la leur.
        std::uint32_t& next = m_nextVersion[target];
        if (next >= std::max<std::uint32_t>(target->desc.maxVersions, 1))
        {
            m_device.error(std::format("buffer volatil « {} » : plus de {} écritures dans une "
                                       "command list",
                                       target->desc.debugName, target->desc.maxVersions));
            return;
        }
        offset += next * alignUp(target->desc.byteSize, UniformOffsetAlignment);
        ++next;
    }
    else if (!m_written.insert(target).second)
    {
        // L'écriture passe par la file avant toute la command list : une seconde écriture
        // effacerait la première avant même que les dessins entre les deux ne s'exécutent.
        m_device.error(std::format("buffer « {} » écrit deux fois dans la même command list : non "
                                   "pris en charge par le backend WebGPU",
                                   target->desc.debugName));
        return;
    }
    // WebGPU n'écrit que des multiples de 4 octets.
    if (dataSize % 4 != 0)
    {
        std::vector<std::byte> padded(alignUp(dataSize, 4));
        std::memcpy(padded.data(), data, dataSize);
        m_device.queue.WriteBuffer(target->buffer, offset, padded.data(), padded.size());
        return;
    }
    m_device.queue.WriteBuffer(target->buffer, offset, data, dataSize);
}

void CommandList::copyBuffer(nvrhi::IBuffer* dest, uint64_t destOffsetBytes, nvrhi::IBuffer* src,
                             uint64_t srcOffsetBytes, uint64_t dataSizeBytes)
{
    endPass();
    m_encoder.CopyBufferToBuffer(static_cast<Buffer*>(src)->buffer, srcOffsetBytes,
                                 static_cast<Buffer*>(dest)->buffer, destOffsetBytes,
                                 dataSizeBytes);
}

uint32_t CommandList::currentOffsetOf(const Buffer& buffer) const
{
    const auto next = m_nextVersion.find(&buffer);
    const std::uint32_t version =
        next == m_nextVersion.end() || next->second == 0 ? 0 : next->second - 1;
    return static_cast<std::uint32_t>(version *
                                      alignUp(buffer.desc.byteSize, UniformOffsetAlignment));
}

std::vector<std::uint32_t> CommandList::dynamicOffsetsOf(const BindingSet& set) const
{
    std::vector<std::uint32_t> offsets;
    offsets.reserve(set.volatileBuffers.size());
    for (const nvrhi::BufferHandle& buffer : set.volatileBuffers)
    {
        offsets.push_back(currentOffsetOf(*static_cast<const Buffer*>(buffer.Get())));
    }
    return offsets;
}

void CommandList::setGraphicsState(const nvrhi::GraphicsState& state)
{
    m_graphics = state;
}

void CommandList::applyGraphicsState()
{
    beginPass(m_graphics.framebuffer);
    const auto* pipeline = static_cast<const GraphicsPipeline*>(m_graphics.pipeline);
    m_pass.SetPipeline(pipeline->pipeline);
    for (std::uint32_t group = 0; group < pipeline->groups.groupCount; ++group)
    {
        m_pass.SetBindGroup(group, m_device.emptyGroup);
    }
    for (std::size_t i = 0; i < m_graphics.bindings.size(); ++i)
    {
        const auto* set = static_cast<const BindingSet*>(m_graphics.bindings[i]);
        const std::vector<std::uint32_t> offsets = dynamicOffsetsOf(*set);
        m_pass.SetBindGroup(pipeline->groups.groupOf[i], set->group, offsets.size(),
                            offsets.data());
    }
    for (const nvrhi::VertexBufferBinding& binding : m_graphics.vertexBuffers)
    {
        m_pass.SetVertexBuffer(binding.slot, static_cast<Buffer*>(binding.buffer)->buffer,
                               binding.offset);
    }
    if (m_graphics.indexBuffer.buffer != nullptr)
    {
        m_pass.SetIndexBuffer(static_cast<Buffer*>(m_graphics.indexBuffer.buffer)->buffer,
                              m_graphics.indexBuffer.format == nvrhi::Format::R16_UINT
                                  ? wgpu::IndexFormat::Uint16
                                  : wgpu::IndexFormat::Uint32,
                              m_graphics.indexBuffer.offset);
    }
    if (!m_graphics.viewport.viewports.empty())
    {
        const nvrhi::Viewport& viewport = m_graphics.viewport.viewports[0];
        m_pass.SetViewport(viewport.minX, viewport.minY, viewport.width(), viewport.height(),
                           viewport.minZ, viewport.maxZ);
    }
    if (!m_graphics.viewport.scissorRects.empty())
    {
        const nvrhi::Rect& scissor = m_graphics.viewport.scissorRects[0];
        m_pass.SetScissorRect(static_cast<std::uint32_t>(scissor.minX),
                              static_cast<std::uint32_t>(scissor.minY),
                              static_cast<std::uint32_t>(scissor.width()),
                              static_cast<std::uint32_t>(scissor.height()));
    }
}

void CommandList::draw(const nvrhi::DrawArguments& args)
{
    applyGraphicsState();
    m_pass.Draw(args.vertexCount, args.instanceCount, args.startVertexLocation,
                args.startInstanceLocation);
}

void CommandList::drawIndexed(const nvrhi::DrawArguments& args)
{
    applyGraphicsState();
    m_pass.DrawIndexed(args.vertexCount, args.instanceCount, args.startIndexLocation,
                       static_cast<std::int32_t>(args.startVertexLocation),
                       args.startInstanceLocation);
}

void CommandList::setComputeState(const nvrhi::ComputeState& state)
{
    m_compute = state;
}

void CommandList::dispatch(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ)
{
    endPass();
    const auto* pipeline = static_cast<const ComputePipeline*>(m_compute.pipeline);
    wgpu::ComputePassEncoder pass = m_encoder.BeginComputePass();
    pass.SetPipeline(pipeline->pipeline);
    for (std::uint32_t group = 0; group < pipeline->groups.groupCount; ++group)
    {
        pass.SetBindGroup(group, m_device.emptyGroup);
    }
    for (std::size_t i = 0; i < m_compute.bindings.size(); ++i)
    {
        const auto* set = static_cast<const BindingSet*>(m_compute.bindings[i]);
        const std::vector<std::uint32_t> offsets = dynamicOffsetsOf(*set);
        pass.SetBindGroup(pipeline->groups.groupOf[i], set->group, offsets.size(), offsets.data());
    }
    pass.DispatchWorkgroups(groupsX, groupsY, groupsZ);
    pass.End();
}

void CommandList::clearTextureUInt(nvrhi::ITexture*, nvrhi::TextureSubresourceSet, uint32_t)
{
    unsupported("clearTextureUInt");
}

void CommandList::copyTexture(nvrhi::ITexture*, const nvrhi::TextureSlice&, nvrhi::IStagingTexture*,
                              const nvrhi::TextureSlice&)
{
    unsupported("la copie d'une texture de relecture vers une texture");
}

void CommandList::resolveTexture(nvrhi::ITexture*, const nvrhi::TextureSubresourceSet&,
                                 nvrhi::ITexture*, const nvrhi::TextureSubresourceSet&)
{
    unsupported("resolveTexture");
}

void CommandList::clearBufferUInt(nvrhi::IBuffer*, uint32_t)
{
    unsupported("clearBufferUInt");
}

void CommandList::clearSamplerFeedbackTexture(nvrhi::ISamplerFeedbackTexture*)
{
    unsupported("le sampler feedback");
}

void CommandList::decodeSamplerFeedbackTexture(nvrhi::IBuffer*, nvrhi::ISamplerFeedbackTexture*,
                                               nvrhi::Format)
{
    unsupported("le sampler feedback");
}

void CommandList::setSamplerFeedbackTextureState(nvrhi::ISamplerFeedbackTexture*,
                                                 nvrhi::ResourceStates)
{
    unsupported("le sampler feedback");
}

void CommandList::setPushConstants(const void*, size_t)
{
    unsupported("les push constants");
}

void CommandList::drawIndirect(uint32_t, uint32_t)
{
    unsupported("drawIndirect");
}

void CommandList::drawIndexedIndirect(uint32_t, uint32_t)
{
    unsupported("drawIndexedIndirect");
}

void CommandList::drawIndexedIndirectCount(uint32_t, uint32_t, uint32_t)
{
    unsupported("drawIndexedIndirectCount");
}

void CommandList::dispatchIndirect(uint32_t)
{
    unsupported("dispatchIndirect");
}

void CommandList::setMeshletState(const nvrhi::MeshletState&)
{
    unsupported("les meshlets");
}

void CommandList::dispatchMesh(uint32_t, uint32_t, uint32_t)
{
    unsupported("les meshlets");
}

void CommandList::setRayTracingState(const nvrhi::rt::State&)
{
    unsupported("le ray tracing");
}

void CommandList::dispatchRays(const nvrhi::rt::DispatchRaysArguments&)
{
    unsupported("le ray tracing");
}

void CommandList::buildOpacityMicromap(nvrhi::rt::IOpacityMicromap*,
                                       const nvrhi::rt::OpacityMicromapDesc&)
{
    unsupported("le ray tracing");
}

void CommandList::buildBottomLevelAccelStruct(nvrhi::rt::IAccelStruct*,
                                              const nvrhi::rt::GeometryDesc*, size_t,
                                              nvrhi::rt::AccelStructBuildFlags)
{
    unsupported("le ray tracing");
}

void CommandList::compactBottomLevelAccelStructs()
{
    unsupported("le ray tracing");
}

void CommandList::buildTopLevelAccelStruct(nvrhi::rt::IAccelStruct*, const nvrhi::rt::InstanceDesc*,
                                           size_t, nvrhi::rt::AccelStructBuildFlags)
{
    unsupported("le ray tracing");
}

void CommandList::executeMultiIndirectClusterOperation(const nvrhi::rt::cluster::OperationDesc&)
{
    unsupported("le ray tracing");
}

void CommandList::buildTopLevelAccelStructFromBuffer(nvrhi::rt::IAccelStruct*, nvrhi::IBuffer*,
                                                     uint64_t, size_t,
                                                     nvrhi::rt::AccelStructBuildFlags)
{
    unsupported("le ray tracing");
}

void CommandList::convertCoopVecMatrices(nvrhi::coopvec::ConvertMatrixLayoutDesc const*, size_t)
{
    unsupported("les vecteurs coopératifs");
}

} // namespace levain::gpu::webgpu

namespace levain::gpu::webgpu
{

nvrhi::CommandListHandle Device::createCommandList(const nvrhi::CommandListParameters& params)
{
    return nvrhi::CommandListHandle::Create(new CommandList{*this, params});
}

uint64_t Device::executeCommandLists(nvrhi::ICommandList* const* commandLists, size_t count,
                                     nvrhi::CommandQueue)
{
    std::vector<wgpu::CommandBuffer> buffers;
    buffers.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        buffers.push_back(static_cast<CommandList*>(commandLists[i])->commands);
    }
    queue.Submit(buffers.size(), buffers.data());
    return 0;
}

nvrhi::StagingTextureHandle Device::createStagingTexture(const nvrhi::TextureDesc& desc,
                                                         nvrhi::CpuAccessMode)
{
    // Un buffer que le CPU peut lire, où la copie range le niveau 0, ligne par ligne, chaque ligne
    // alignée sur 256 octets comme WebGPU l'exige.
    const nvrhi::FormatInfo& info = nvrhi::getFormatInfo(desc.format);
    const std::uint32_t blocksPerRow = (desc.width + info.blockSize - 1) / info.blockSize;
    const std::uint32_t rows = (desc.height + info.blockSize - 1) / info.blockSize;
    const auto bytesPerRow =
        static_cast<std::uint32_t>(alignUp(std::uint64_t{blocksPerRow} * info.bytesPerBlock, 256));
    wgpu::BufferDescriptor bufferDesc{};
    bufferDesc.size = std::uint64_t{bytesPerRow} * rows;
    bufferDesc.usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst;
    wgpu::Buffer buffer = device.CreateBuffer(&bufferDesc);
    return nvrhi::StagingTextureHandle::Create(
        new StagingTexture{desc, std::move(buffer), bytesPerRow});
}

void* Device::mapStagingTexture(nvrhi::IStagingTexture* texture, const nvrhi::TextureSlice&,
                                nvrhi::CpuAccessMode, size_t* rowPitch)
{
    auto* staging = static_cast<StagingTexture*>(texture);
#ifdef __EMSCRIPTEN__
    // Le navigateur ne rend un buffer lisible que plus tard, par callback : la relecture
    // synchrone de NVRHI n'y existe pas. --capture reste un outil natif (ADR-0023, point 4).
    (void)staging;
    (void)rowPitch;
    error("relecture synchrone impossible dans le navigateur");
    return nullptr;
#else
    bool mapped = false;
    instance.WaitAny(
        staging->buffer.MapAsync(wgpu::MapMode::Read, 0, staging->buffer.GetSize(),
                                 wgpu::CallbackMode::WaitAnyOnly,
                                 [&mapped](wgpu::MapAsyncStatus status, wgpu::StringView)
                                 { mapped = status == wgpu::MapAsyncStatus::Success; }),
        UINT64_MAX);
    if (!mapped)
    {
        error("relecture : le buffer ne s'est pas ouvert au CPU");
        return nullptr;
    }
    *rowPitch = staging->bytesPerRow;
    return const_cast<void*>(staging->buffer.GetConstMappedRange());
#endif
}

void Device::unmapStagingTexture(nvrhi::IStagingTexture* texture)
{
    static_cast<StagingTexture*>(texture)->buffer.Unmap();
}

nvrhi::EventQueryHandle Device::createEventQuery()
{
    return nvrhi::EventQueryHandle::Create(new EventQuery{});
}

nvrhi::TimerQueryHandle Device::createTimerQuery()
{
    return nvrhi::TimerQueryHandle::Create(new TimerQuery{});
}

} // namespace levain::gpu::webgpu
