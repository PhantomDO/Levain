#pragma once

// Le backend WebGPU de NVRHI (ADR-0023) : les interfaces de NVRHI implémentées sur webgpu.h. En
// natif sur Dawn, pour le développer et le tester ; dans le navigateur, par emdawnwebgpu.
//
// Ce que le moteur n'utilise pas (heaps, ray tracing, meshlets, sampler feedback, coopvec…) répond
// « non supporté » : nullptr, false, ou rien. Le moteur interroge queryFeatureSupport avant de s'en
// servir, et choisit une autre technique.

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

#include <nvrhi/common/aftermath.h>
#include <nvrhi/nvrhi.h>
#include <webgpu/webgpu_cpp.h>

namespace levain::gpu::webgpu
{

/// Nos types d'objets natifs, pour `getNativeObject` : NVRHI occupe les plages 0x0000 à 0x0003
/// (D3D11, D3D12, Vulkan) ; WebGPU prend 0x0010. La couche de validation les relaie au device.
namespace object_types
{
inline constexpr nvrhi::ObjectType Instance = 0x00100001;
inline constexpr nvrhi::ObjectType Adapter = 0x00100002;
inline constexpr nvrhi::ObjectType Device = 0x00100003;
} // namespace object_types

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

class Framebuffer final : public nvrhi::RefCounter<nvrhi::IFramebuffer>
{
public:
    explicit Framebuffer(const nvrhi::FramebufferDesc& desc) : desc{desc}, info{desc} {}

    [[nodiscard]] const nvrhi::FramebufferDesc& getDesc() const override { return desc; }

    [[nodiscard]] const nvrhi::FramebufferInfoEx& getFramebufferInfo() const override
    {
        return info;
    }

    nvrhi::FramebufferDesc desc;
    nvrhi::FramebufferInfoEx info;
};

/// Ce que le layout de NVRHI ne dit pas et que WebGPU veut savoir, à tel binding : une texture de
/// profondeur ou un sampler de comparaison (les ombres, M5.3), une cubemap, une texture écrite par
/// un compute et son format (l'IBL, M5.4). Vulkan s'en passe.
struct BindingHint
{
    enum class Kind : std::uint8_t
    {
        DepthTexture,
        ComparisonSampler,
        CubeTexture,
        ArrayTexture, ///< Un tableau de textures, lu couche par couche (les couches du terrain).
        StorageTexture2D,
        StorageTexture2DArray, ///< Une cubemap s'écrit face par face, comme un tableau 2D.
    };

    std::uint32_t binding = 0;
    Kind kind = Kind::DepthTexture;
    wgpu::TextureFormat format = wgpu::TextureFormat::Undefined; ///< D'une storage texture.

    auto operator<=>(const BindingHint&) const = default;
};

/// Les indications d'un groupe, triées : deux layouts aux mêmes entrées sont interchangeables en
/// WebGPU, que l'un vienne des ressources liées et l'autre du shader.
using BindingHints = std::vector<BindingHint>;

/// Un binding layout devient un bind group layout, et ses variantes selon les indications.
class BindingLayout final : public nvrhi::RefCounter<nvrhi::IBindingLayout>
{
public:
    BindingLayout(const nvrhi::BindingLayoutDesc& desc, wgpu::BindGroupLayout layout,
                  std::vector<wgpu::BindGroupLayoutEntry> entries)
        : desc{desc}, layout{std::move(layout)}, entries{std::move(entries)}
    {
    }

    /// Le layout de WebGPU où s'appliquent les indications `hints` : `layout` si aucune, sinon une
    /// variante, créée une fois.
    [[nodiscard]] wgpu::BindGroupLayout layoutFor(const wgpu::Device& device,
                                                  const BindingHints& hints) const;

    [[nodiscard]] const nvrhi::BindingLayoutDesc* getDesc() const override { return &desc; }

    [[nodiscard]] const nvrhi::BindlessLayoutDesc* getBindlessDesc() const override
    {
        return nullptr;
    }

    nvrhi::BindingLayoutDesc desc;
    wgpu::BindGroupLayout layout;
    std::vector<wgpu::BindGroupLayoutEntry> entries;
    mutable std::map<BindingHints, wgpu::BindGroupLayout> variants;
};

/// Un binding set devient un bind group. `volatileBuffers` : ses buffers de constantes volatils,
/// dans l'ordre de leurs offsets dynamiques ; la command list donne à chacun sa version courante.
class BindingSet final : public nvrhi::RefCounter<nvrhi::IBindingSet>
{
public:
    BindingSet(const nvrhi::BindingSetDesc& desc, nvrhi::BindingLayoutHandle layout,
               wgpu::BindGroup group, std::vector<nvrhi::BufferHandle> volatileBuffers)
        : desc{desc}, layout{std::move(layout)}, group{std::move(group)},
          volatileBuffers{std::move(volatileBuffers)}
    {
        // Un BindingSetItem ne tient sa ressource que par un pointeur brut : c'est au binding set
        // de la garder vivante, comme le fait le backend Vulkan de NVRHI (ses `resources`). Sans
        // cela, un buffer qu'on ne garde que par son binding set disparaît à la création.
        resources.reserve(desc.bindings.size());
        for (const nvrhi::BindingSetItem& item : desc.bindings)
        {
            resources.emplace_back(item.resourceHandle);
        }
    }

    [[nodiscard]] const nvrhi::BindingSetDesc* getDesc() const override { return &desc; }

    [[nodiscard]] nvrhi::IBindingLayout* getLayout() const override { return layout; }

    nvrhi::BindingSetDesc desc;
    nvrhi::BindingLayoutHandle layout;
    wgpu::BindGroup group;
    std::vector<nvrhi::BufferHandle> volatileBuffers;
    std::vector<nvrhi::ResourceHandle> resources;
};

/// Ce que partagent les pipelines : le numéro de groupe de chacun de leurs binding layouts, dans
/// l'ordre de `bindingLayouts` (celui des binding sets de la command list), et le nombre de
/// groupes.
struct PipelineGroups
{
    std::vector<uint32_t> groupOf;
    uint32_t groupCount = 0;
};

class GraphicsPipeline final : public nvrhi::RefCounter<nvrhi::IGraphicsPipeline>
{
public:
    GraphicsPipeline(const nvrhi::GraphicsPipelineDesc& desc, const nvrhi::FramebufferInfo& info,
                     wgpu::RenderPipeline pipeline, PipelineGroups groups)
        : desc{desc}, info{info}, pipeline{std::move(pipeline)}, groups{std::move(groups)}
    {
    }

    [[nodiscard]] const nvrhi::GraphicsPipelineDesc& getDesc() const override { return desc; }

    [[nodiscard]] const nvrhi::FramebufferInfo& getFramebufferInfo() const override { return info; }

    nvrhi::GraphicsPipelineDesc desc;
    nvrhi::FramebufferInfo info;
    wgpu::RenderPipeline pipeline;
    PipelineGroups groups;
};

class ComputePipeline final : public nvrhi::RefCounter<nvrhi::IComputePipeline>
{
public:
    ComputePipeline(const nvrhi::ComputePipelineDesc& desc, wgpu::ComputePipeline pipeline,
                    PipelineGroups groups)
        : desc{desc}, pipeline{std::move(pipeline)}, groups{std::move(groups)}
    {
    }

    [[nodiscard]] const nvrhi::ComputePipelineDesc& getDesc() const override { return desc; }

    nvrhi::ComputePipelineDesc desc;
    wgpu::ComputePipeline pipeline;
    PipelineGroups groups;
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

    nvrhi::FramebufferHandle createFramebuffer(const nvrhi::FramebufferDesc& desc) override;
    // bindings.cpp
    nvrhi::BindingLayoutHandle createBindingLayout(const nvrhi::BindingLayoutDesc& desc) override;
    nvrhi::BindingSetHandle createBindingSet(const nvrhi::BindingSetDesc& desc,
                                             nvrhi::IBindingLayout* layout) override;
    // pipelines.cpp
    nvrhi::GraphicsPipelineHandle
    createGraphicsPipeline(const nvrhi::GraphicsPipelineDesc& desc,
                           nvrhi::FramebufferInfo const& info) override;
    nvrhi::GraphicsPipelineHandle createGraphicsPipeline(const nvrhi::GraphicsPipelineDesc& desc,
                                                         nvrhi::IFramebuffer* framebuffer) override;
    nvrhi::ComputePipelineHandle
    createComputePipeline(const nvrhi::ComputePipelineDesc& desc) override;

    nvrhi::GraphicsAPI getGraphicsAPI() override { return nvrhi::GraphicsAPI::WEBGPU; }

    bool queryFeatureSupport(nvrhi::Feature feature, void* info, size_t infoSize) override;
    nvrhi::FormatSupport queryFormatSupport(nvrhi::Format format) override;
    bool waitForIdle() override;

    void runGarbageCollection() override {}

    nvrhi::IMessageCallback* getMessageCallback() override { return m_messageCallback; }

    nvrhi::Object getNativeObject(nvrhi::ObjectType type) override;

    // commandlist.cpp : command lists, relecture, requêtes.
    nvrhi::CommandListHandle createCommandList(const nvrhi::CommandListParameters& params) override;
    uint64_t executeCommandLists(nvrhi::ICommandList* const* commandLists, size_t count,
                                 nvrhi::CommandQueue queue) override;
    nvrhi::StagingTextureHandle createStagingTexture(const nvrhi::TextureDesc& desc,
                                                     nvrhi::CpuAccessMode access) override;
    void* mapStagingTexture(nvrhi::IStagingTexture* texture, const nvrhi::TextureSlice& slice,
                            nvrhi::CpuAccessMode access, size_t* rowPitch) override;
    void unmapStagingTexture(nvrhi::IStagingTexture* texture) override;
    nvrhi::EventQueryHandle createEventQuery() override;

    void setEventQuery(nvrhi::IEventQuery*, nvrhi::CommandQueue) override {}

    bool pollEventQuery(nvrhi::IEventQuery*) override { return true; }

    void waitEventQuery(nvrhi::IEventQuery*) override { waitForIdle(); }

    void resetEventQuery(nvrhi::IEventQuery*) override {}

    nvrhi::TimerQueryHandle createTimerQuery() override;

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

    void* mapBuffer(nvrhi::IBuffer* buffer, nvrhi::CpuAccessMode access) override;

    void unmapBuffer(nvrhi::IBuffer* buffer) override;

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

    /// Le groupe vide, lié là où un pipeline n'a pas de binding layout (l'espace de registres 1 de
    /// la passe des meshes, ADR-0013) : WebGPU veut un groupe à chaque numéro qu'il utilise.
    wgpu::BindGroupLayout emptyLayout;
    wgpu::BindGroup emptyGroup;

    /// Les groupes d'un pipeline qui utilise `layouts` (pipelines.cpp).
    [[nodiscard]] PipelineGroups groupsOf(const nvrhi::BindingLayoutVector& layouts) const;
    /// Son pipeline layout WebGPU : les bind group layouts, et le groupe vide dans les trous. Les
    /// textures de profondeur et samplers de comparaison viennent des déclarations de `shaders`.
    [[nodiscard]] wgpu::PipelineLayout
    pipelineLayoutOf(const nvrhi::BindingLayoutVector& layouts, const PipelineGroups& groups,
                     std::span<const Shader* const> shaders) const;

    /// Signale une erreur à NVRHI comme le font ses backends : par le callback de messages.
    void error(const std::string& message) const;
    /// Ouvre un buffer `MapRead` au CPU, en attendant le GPU : en natif seulement, le navigateur
    /// ne sait pas attendre (commandlist.cpp). `nullptr` et une erreur signalée sinon.
    [[nodiscard]] void* mapForRead(wgpu::Buffer& buffer);

private:
    nvrhi::IMessageCallback* m_messageCallback;
    nvrhi::AftermathCrashDumpHelper m_aftermath; ///< Vide : Aftermath est propre à NVIDIA.
};

} // namespace levain::gpu::webgpu
