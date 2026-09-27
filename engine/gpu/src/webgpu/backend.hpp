#pragma once

// Le backend WebGPU de NVRHI (ADR-0023) : les interfaces de NVRHI implémentées sur webgpu.h. En
// natif sur Dawn, pour le développer et le tester ; dans le navigateur, par emdawnwebgpu.
//
// Ce que le moteur n'utilise pas (heaps, ray tracing, meshlets, sampler feedback, coopvec…) répond
// « non supporté » : nullptr, false, ou rien. Le moteur interroge queryFeatureSupport avant de s'en
// servir, et choisit une autre technique.

#include <cstdint>
#include <string>
#include <vector>

#include <nvrhi/common/aftermath.h>
#include <nvrhi/nvrhi.h>
#include <webgpu/webgpu_cpp.h>

namespace levain::gpu::webgpu
{

class Buffer final : public nvrhi::RefCounter<nvrhi::IBuffer>
{
public:
    Buffer(const nvrhi::BufferDesc& desc, wgpu::Buffer buffer)
        : desc{desc}, buffer{std::move(buffer)}
    {
    }

    [[nodiscard]] const nvrhi::BufferDesc& getDesc() const override { return desc; }

    [[nodiscard]] nvrhi::GpuVirtualAddress getGpuVirtualAddress() const override { return 0; }

    nvrhi::BufferDesc desc;
    wgpu::Buffer buffer;
};

class Texture final : public nvrhi::RefCounter<nvrhi::ITexture>
{
public:
    Texture(const nvrhi::TextureDesc& desc, wgpu::Texture texture)
        : desc{desc}, texture{std::move(texture)}
    {
    }

    [[nodiscard]] const nvrhi::TextureDesc& getDesc() const override { return desc; }

    nvrhi::Object getNativeView(nvrhi::ObjectType, nvrhi::Format, nvrhi::TextureSubresourceSet,
                                nvrhi::TextureDimension, bool) override
    {
        return nullptr;
    }

    nvrhi::TextureDesc desc;
    wgpu::Texture texture;
};

class Sampler final : public nvrhi::RefCounter<nvrhi::ISampler>
{
public:
    Sampler(const nvrhi::SamplerDesc& desc, wgpu::Sampler sampler)
        : desc{desc}, sampler{std::move(sampler)}
    {
    }

    [[nodiscard]] const nvrhi::SamplerDesc& getDesc() const override { return desc; }

    nvrhi::SamplerDesc desc;
    wgpu::Sampler sampler;
};

/// Un shader WGSL. Le point d'entrée est celui de `desc.entryName` : WGSL garde les noms des
/// fonctions, là où slangc nomme « main » celui d'un SPIR-V.
class Shader final : public nvrhi::RefCounter<nvrhi::IShader>
{
public:
    Shader(const nvrhi::ShaderDesc& desc, std::string wgsl, wgpu::ShaderModule module)
        : desc{desc}, wgsl{std::move(wgsl)}, module{std::move(module)}
    {
    }

    [[nodiscard]] const nvrhi::ShaderDesc& getDesc() const override { return desc; }

    void getBytecode(const void** bytecode, size_t* size) const override
    {
        *bytecode = wgsl.data();
        *size = wgsl.size();
    }

    nvrhi::ShaderDesc desc;
    std::string wgsl;
    wgpu::ShaderModule module;
};

class InputLayout final : public nvrhi::RefCounter<nvrhi::IInputLayout>
{
public:
    explicit InputLayout(std::vector<nvrhi::VertexAttributeDesc> attributes)
        : attributes{std::move(attributes)}
    {
    }

    [[nodiscard]] uint32_t getNumAttributes() const override
    {
        return static_cast<uint32_t>(attributes.size());
    }

    [[nodiscard]] const nvrhi::VertexAttributeDesc* getAttributeDesc(uint32_t index) const override
    {
        return index < attributes.size() ? &attributes[index] : nullptr;
    }

    std::vector<nvrhi::VertexAttributeDesc> attributes;
};

/// Le device : l'adaptateur et le device WebGPU, et la file où tout est soumis.
class Device final : public nvrhi::RefCounter<nvrhi::IDevice>
{
public:
    Device(wgpu::Instance instance, wgpu::Adapter adapter, wgpu::Device device,
           nvrhi::IMessageCallback* messageCallback);

    // Ce que le moteur utilise (device.cpp).
    nvrhi::TextureHandle createTexture(const nvrhi::TextureDesc& desc) override;
    nvrhi::BufferHandle createBuffer(const nvrhi::BufferDesc& desc) override;
    nvrhi::ShaderHandle createShader(const nvrhi::ShaderDesc& desc, const void* binary,
                                     size_t binarySize) override;
    nvrhi::SamplerHandle createSampler(const nvrhi::SamplerDesc& desc) override;
    nvrhi::InputLayoutHandle createInputLayout(const nvrhi::VertexAttributeDesc* attributes,
                                               uint32_t attributeCount,
                                               nvrhi::IShader* vertexShader) override;

    nvrhi::GraphicsAPI getGraphicsAPI() override { return nvrhi::GraphicsAPI::WEBGPU; }

    bool queryFeatureSupport(nvrhi::Feature feature, void* info, size_t infoSize) override;
    nvrhi::FormatSupport queryFormatSupport(nvrhi::Format format) override;
    bool waitForIdle() override;

    void runGarbageCollection() override {}

    nvrhi::IMessageCallback* getMessageCallback() override { return m_messageCallback; }

    nvrhi::Object getNativeObject(nvrhi::ObjectType) override { return nullptr; }

    // À venir (#184, partie B) : bindings, pipelines, command lists, relecture, requêtes.
    nvrhi::StagingTextureHandle createStagingTexture(const nvrhi::TextureDesc&,
                                                     nvrhi::CpuAccessMode) override
    {
        return nullptr;
    }

    void* mapStagingTexture(nvrhi::IStagingTexture*, const nvrhi::TextureSlice&,
                            nvrhi::CpuAccessMode, size_t*) override
    {
        return nullptr;
    }

    void unmapStagingTexture(nvrhi::IStagingTexture*) override {}

    nvrhi::FramebufferHandle createFramebuffer(const nvrhi::FramebufferDesc&) override
    {
        return nullptr;
    }

    nvrhi::GraphicsPipelineHandle createGraphicsPipeline(const nvrhi::GraphicsPipelineDesc&,
                                                         nvrhi::FramebufferInfo const&) override
    {
        return nullptr;
    }

    nvrhi::GraphicsPipelineHandle createGraphicsPipeline(const nvrhi::GraphicsPipelineDesc&,
                                                         nvrhi::IFramebuffer*) override
    {
        return nullptr;
    }

    nvrhi::ComputePipelineHandle createComputePipeline(const nvrhi::ComputePipelineDesc&) override
    {
        return nullptr;
    }

    nvrhi::BindingLayoutHandle createBindingLayout(const nvrhi::BindingLayoutDesc&) override
    {
        return nullptr;
    }

    nvrhi::BindingSetHandle createBindingSet(const nvrhi::BindingSetDesc&,
                                             nvrhi::IBindingLayout*) override
    {
        return nullptr;
    }

    nvrhi::CommandListHandle createCommandList(const nvrhi::CommandListParameters&) override
    {
        return nullptr;
    }

    uint64_t executeCommandLists(nvrhi::ICommandList* const*, size_t, nvrhi::CommandQueue) override
    {
        return 0;
    }

    nvrhi::EventQueryHandle createEventQuery() override { return nullptr; }

    void setEventQuery(nvrhi::IEventQuery*, nvrhi::CommandQueue) override {}

    bool pollEventQuery(nvrhi::IEventQuery*) override { return true; }

    void waitEventQuery(nvrhi::IEventQuery*) override {}

    void resetEventQuery(nvrhi::IEventQuery*) override {}

    nvrhi::TimerQueryHandle createTimerQuery() override { return nullptr; }

    bool pollTimerQuery(nvrhi::ITimerQuery*) override { return false; }

    float getTimerQueryTime(nvrhi::ITimerQuery*) override { return 0.0f; }

    void resetTimerQuery(nvrhi::ITimerQuery*) override {}

    // Non supporté par WebGPU, ou par ce backend : le moteur s'en passe (queryFeatureSupport).
    nvrhi::HeapHandle createHeap(const nvrhi::HeapDesc&) override { return nullptr; }

    nvrhi::MemoryRequirements getTextureMemoryRequirements(nvrhi::ITexture*) override { return {}; }

    bool bindTextureMemory(nvrhi::ITexture*, nvrhi::IHeap*, uint64_t) override { return false; }

    nvrhi::TextureHandle createHandleForNativeTexture(nvrhi::ObjectType, nvrhi::Object,
                                                      const nvrhi::TextureDesc&) override
    {
        return nullptr;
    }

    void getTextureTiling(nvrhi::ITexture*, uint32_t*, nvrhi::PackedMipDesc*, nvrhi::TileShape*,
                          uint32_t*, nvrhi::SubresourceTiling*) override
    {
    }

    void updateTextureTileMappings(nvrhi::ITexture*, const nvrhi::TextureTilesMapping*, uint32_t,
                                   nvrhi::CommandQueue) override
    {
    }

    nvrhi::SamplerFeedbackTextureHandle
    createSamplerFeedbackTexture(nvrhi::ITexture*,
                                 const nvrhi::SamplerFeedbackTextureDesc&) override
    {
        return nullptr;
    }

    nvrhi::SamplerFeedbackTextureHandle
    createSamplerFeedbackForNativeTexture(nvrhi::ObjectType, nvrhi::Object,
                                          nvrhi::ITexture*) override
    {
        return nullptr;
    }

    void* mapBuffer(nvrhi::IBuffer*, nvrhi::CpuAccessMode) override { return nullptr; }

    void unmapBuffer(nvrhi::IBuffer*) override {}

    nvrhi::MemoryRequirements getBufferMemoryRequirements(nvrhi::IBuffer*) override { return {}; }

    bool bindBufferMemory(nvrhi::IBuffer*, nvrhi::IHeap*, uint64_t) override { return false; }

    nvrhi::BufferHandle createHandleForNativeBuffer(nvrhi::ObjectType, nvrhi::Object,
                                                    const nvrhi::BufferDesc&) override
    {
        return nullptr;
    }

    nvrhi::ShaderHandle createShaderSpecialization(nvrhi::IShader*,
                                                   const nvrhi::ShaderSpecialization*,
                                                   uint32_t) override
    {
        return nullptr;
    }

    nvrhi::ShaderLibraryHandle createShaderLibrary(const void*, size_t) override { return nullptr; }

    nvrhi::MeshletPipelineHandle createMeshletPipeline(const nvrhi::MeshletPipelineDesc&,
                                                       nvrhi::FramebufferInfo const&) override
    {
        return nullptr;
    }

    nvrhi::MeshletPipelineHandle createMeshletPipeline(const nvrhi::MeshletPipelineDesc&,
                                                       nvrhi::IFramebuffer*) override
    {
        return nullptr;
    }

    nvrhi::rt::PipelineHandle createRayTracingPipeline(const nvrhi::rt::PipelineDesc&) override
    {
        return nullptr;
    }

    nvrhi::BindingLayoutHandle createBindlessLayout(const nvrhi::BindlessLayoutDesc&) override
    {
        return nullptr;
    }

    nvrhi::DescriptorTableHandle createDescriptorTable(nvrhi::IBindingLayout*) override
    {
        return nullptr;
    }

    void resizeDescriptorTable(nvrhi::IDescriptorTable*, uint32_t, bool) override {}

    bool writeDescriptorTable(nvrhi::IDescriptorTable*, const nvrhi::BindingSetItem&) override
    {
        return false;
    }

    nvrhi::rt::OpacityMicromapHandle
    createOpacityMicromap(const nvrhi::rt::OpacityMicromapDesc&) override
    {
        return nullptr;
    }

    nvrhi::rt::AccelStructHandle createAccelStruct(const nvrhi::rt::AccelStructDesc&) override
    {
        return nullptr;
    }

    nvrhi::MemoryRequirements getAccelStructMemoryRequirements(nvrhi::rt::IAccelStruct*) override
    {
        return {};
    }

    nvrhi::rt::cluster::OperationSizeInfo
    getClusterOperationSizeInfo(const nvrhi::rt::cluster::OperationParams&) override
    {
        return {};
    }

    bool bindAccelStructMemory(nvrhi::rt::IAccelStruct*, nvrhi::IHeap*, uint64_t) override
    {
        return false;
    }

    void queueWaitForCommandList(nvrhi::CommandQueue, nvrhi::CommandQueue, uint64_t) override {}

    nvrhi::coopvec::DeviceFeatures queryCoopVecFeatures() override { return {}; }

    size_t getCoopVecMatrixSize(nvrhi::coopvec::DataType, nvrhi::coopvec::MatrixLayout, int,
                                int) override
    {
        return 0;
    }

    nvrhi::Object getNativeQueue(nvrhi::ObjectType, nvrhi::CommandQueue) override
    {
        return nullptr;
    }

    bool isAftermathEnabled() override { return false; }

    nvrhi::AftermathCrashDumpHelper& getAftermathCrashDumpHelper() override { return m_aftermath; }

    wgpu::Instance instance;
    wgpu::Adapter adapter;
    wgpu::Device device;
    wgpu::Queue queue;

private:
    /// Signale une erreur à NVRHI comme le font ses backends : par le callback de messages.
    void error(const std::string& message) const;

    nvrhi::IMessageCallback* m_messageCallback;
    nvrhi::AftermathCrashDumpHelper m_aftermath; ///< Vide : Aftermath est propre à NVIDIA.
};

} // namespace levain::gpu::webgpu
