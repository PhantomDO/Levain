#pragma once

// La command list de NVRHI sur un wgpu::CommandEncoder (ADR-0023).
//
// Les passes de rendu de WebGPU encadrent les dessins : la command list en ouvre une au premier
// dessin sur un framebuffer, et la ferme dès qu'arrive une commande qui n'y a pas sa place (copie,
// dispatch, effacement, autre framebuffer). Les barrières, que NVRHI place sous Vulkan, WebGPU les
// place lui-même : les méthodes d'états de ressources ne font rien.
//
// Les écritures (writeBuffer, writeTexture) passent par la file de WebGPU au moment où on les
// enregistre : elles s'appliquent avant cette command list, et après les précédentes. Une constante
// volatile reçoit une nouvelle version à chaque écriture (ADR-0023, point 1).

#include <cstdint>
#include <map>
#include <set>
#include <vector>

#include "backend.hpp"

namespace levain::gpu::webgpu
{

class StagingTexture final : public nvrhi::RefCounter<nvrhi::IStagingTexture>
{
public:
    StagingTexture(const nvrhi::TextureDesc& desc, wgpu::Buffer buffer, uint32_t bytesPerRow)
        : desc{desc}, buffer{std::move(buffer)}, bytesPerRow{bytesPerRow}
    {
    }

    [[nodiscard]] const nvrhi::TextureDesc& getDesc() const override { return desc; }

    nvrhi::TextureDesc desc;
    wgpu::Buffer buffer;
    uint32_t bytesPerRow; ///< Aligné sur 256 octets, comme WebGPU l'exige pour une copie.
};

/// WebGPU n'a de requêtes de temps qu'avec la fonctionnalité `timestamp-query` : pas encore
/// branchée. Ces requêtes ne rendent jamais de mesure, et le moteur n'affiche pas de temps GPU.
class TimerQuery final : public nvrhi::RefCounter<nvrhi::ITimerQuery>
{
};

class EventQuery final : public nvrhi::RefCounter<nvrhi::IEventQuery>
{
};

class CommandList final : public nvrhi::RefCounter<nvrhi::ICommandList>
{
public:
    CommandList(Device& device, const nvrhi::CommandListParameters& parameters)
        : m_device{device}, m_parameters{parameters}
    {
    }

    /// Le travail enregistré depuis `close`, à soumettre.
    wgpu::CommandBuffer commands;

    void open() override;
    void close() override;
    void clearState() override;
    void clearTextureFloat(nvrhi::ITexture* texture, nvrhi::TextureSubresourceSet subresources,
                           const nvrhi::Color& color) override;
    void clearDepthStencilTexture(nvrhi::ITexture* texture,
                                  nvrhi::TextureSubresourceSet subresources, bool clearDepth,
                                  float depth, bool clearStencil, uint8_t stencil) override;
    void copyTexture(nvrhi::ITexture* dest, const nvrhi::TextureSlice& destSlice,
                     nvrhi::ITexture* src, const nvrhi::TextureSlice& srcSlice) override;
    void copyTexture(nvrhi::IStagingTexture* dest, const nvrhi::TextureSlice& destSlice,
                     nvrhi::ITexture* src, const nvrhi::TextureSlice& srcSlice) override;
    void writeTexture(nvrhi::ITexture* dest, uint32_t arraySlice, uint32_t mipLevel,
                      const void* data, size_t rowPitch, size_t depthPitch) override;
    void writeBuffer(nvrhi::IBuffer* buffer, const void* data, size_t dataSize,
                     uint64_t destOffsetBytes) override;
    void copyBuffer(nvrhi::IBuffer* dest, uint64_t destOffsetBytes, nvrhi::IBuffer* src,
                    uint64_t srcOffsetBytes, uint64_t dataSizeBytes) override;
    void setGraphicsState(const nvrhi::GraphicsState& state) override;
    void draw(const nvrhi::DrawArguments& args) override;
    void drawIndexed(const nvrhi::DrawArguments& args) override;
    void setComputeState(const nvrhi::ComputeState& state) override;
    void dispatch(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ) override;

    nvrhi::IDevice* getDevice() override { return &m_device; }

    const nvrhi::CommandListParameters& getDesc() override { return m_parameters; }

    // WebGPU place les barrières lui-même ; les requêtes de temps ne mesurent rien (TimerQuery).
    void beginTimerQuery(nvrhi::ITimerQuery*) override {}

    void endTimerQuery(nvrhi::ITimerQuery*) override {}

    void beginMarker(const char*) override {}

    void endMarker() override {}

    void setEnableAutomaticBarriers(bool) override {}

    void setResourceStatesForBindingSet(nvrhi::IBindingSet*) override {}

    void setEnableUavBarriersForTexture(nvrhi::ITexture*, bool) override {}

    void setEnableUavBarriersForBuffer(nvrhi::IBuffer*, bool) override {}

    void beginTrackingTextureState(nvrhi::ITexture*, nvrhi::TextureSubresourceSet,
                                   nvrhi::ResourceStates) override
    {
    }

    void beginTrackingBufferState(nvrhi::IBuffer*, nvrhi::ResourceStates) override {}

    void setTextureState(nvrhi::ITexture*, nvrhi::TextureSubresourceSet,
                         nvrhi::ResourceStates) override
    {
    }

    void setBufferState(nvrhi::IBuffer*, nvrhi::ResourceStates) override {}

    void setAccelStructState(nvrhi::rt::IAccelStruct*, nvrhi::ResourceStates) override {}

    void setPermanentTextureState(nvrhi::ITexture*, nvrhi::ResourceStates) override {}

    void setPermanentBufferState(nvrhi::IBuffer*, nvrhi::ResourceStates) override {}

    void commitBarriers() override {}

    nvrhi::ResourceStates getTextureSubresourceState(nvrhi::ITexture*, nvrhi::ArraySlice,
                                                     nvrhi::MipLevel) override
    {
        return nvrhi::ResourceStates::Common;
    }

    nvrhi::ResourceStates getBufferState(nvrhi::IBuffer*) override
    {
        return nvrhi::ResourceStates::Common;
    }

    // Non supporté (queryFeatureSupport le dit) : un appel est un bug, signalé.
    void clearTextureUInt(nvrhi::ITexture*, nvrhi::TextureSubresourceSet, uint32_t) override;
    void copyTexture(nvrhi::ITexture*, const nvrhi::TextureSlice&, nvrhi::IStagingTexture*,
                     const nvrhi::TextureSlice&) override;
    void resolveTexture(nvrhi::ITexture*, const nvrhi::TextureSubresourceSet&, nvrhi::ITexture*,
                        const nvrhi::TextureSubresourceSet&) override;
    void clearBufferUInt(nvrhi::IBuffer*, uint32_t) override;
    void clearSamplerFeedbackTexture(nvrhi::ISamplerFeedbackTexture*) override;
    void decodeSamplerFeedbackTexture(nvrhi::IBuffer*, nvrhi::ISamplerFeedbackTexture*,
                                      nvrhi::Format) override;
    void setSamplerFeedbackTextureState(nvrhi::ISamplerFeedbackTexture*,
                                        nvrhi::ResourceStates) override;
    void setPushConstants(const void*, size_t) override;
    void drawIndirect(uint32_t, uint32_t) override;
    void drawIndexedIndirect(uint32_t, uint32_t) override;
    void drawIndexedIndirectCount(uint32_t, uint32_t, uint32_t) override;
    void dispatchIndirect(uint32_t) override;
    void setMeshletState(const nvrhi::MeshletState&) override;
    void dispatchMesh(uint32_t, uint32_t, uint32_t) override;
    void setRayTracingState(const nvrhi::rt::State&) override;
    void dispatchRays(const nvrhi::rt::DispatchRaysArguments&) override;
    void buildOpacityMicromap(nvrhi::rt::IOpacityMicromap*,
                              const nvrhi::rt::OpacityMicromapDesc&) override;
    void buildBottomLevelAccelStruct(nvrhi::rt::IAccelStruct*, const nvrhi::rt::GeometryDesc*,
                                     size_t, nvrhi::rt::AccelStructBuildFlags) override;
    void compactBottomLevelAccelStructs() override;
    void buildTopLevelAccelStruct(nvrhi::rt::IAccelStruct*, const nvrhi::rt::InstanceDesc*, size_t,
                                  nvrhi::rt::AccelStructBuildFlags) override;
    void executeMultiIndirectClusterOperation(const nvrhi::rt::cluster::OperationDesc&) override;
    void buildTopLevelAccelStructFromBuffer(nvrhi::rt::IAccelStruct*, nvrhi::IBuffer*, uint64_t,
                                            size_t, nvrhi::rt::AccelStructBuildFlags) override;
    void convertCoopVecMatrices(nvrhi::coopvec::ConvertMatrixLayoutDesc const*, size_t) override;

private:
    /// Ouvre une passe de rendu sur `framebuffer`, qui garde son contenu, si elle ne l'est pas
    /// déjà.
    void beginPass(nvrhi::IFramebuffer* framebuffer);
    void endPass();
    /// Lie l'état graphique enregistré, offsets dynamiques compris : juste avant chaque dessin,
    /// pour qu'une constante écrite après setGraphicsState soit bien celle que lit le dessin.
    void applyGraphicsState();
    /// L'offset de la version courante d'un buffer de constantes volatil.
    [[nodiscard]] uint32_t currentOffsetOf(const Buffer& buffer) const;
    /// Les offsets dynamiques d'un binding set : la version courante de chacune de ses constantes
    /// volatiles, dans l'ordre que WebGPU attend.
    [[nodiscard]] std::vector<uint32_t> dynamicOffsetsOf(const BindingSet& set) const;
    void unsupported(const char* what) const;

    Device& m_device;
    nvrhi::CommandListParameters m_parameters;
    wgpu::CommandEncoder m_encoder;
    wgpu::RenderPassEncoder m_pass;
    nvrhi::IFramebuffer* m_passFramebuffer = nullptr;
    nvrhi::GraphicsState m_graphics;
    nvrhi::ComputeState m_compute;
    /// La prochaine version de chaque buffer volatil écrit dans cette command list.
    std::map<const Buffer*, uint32_t> m_nextVersion;
    /// Les buffers ordinaires déjà écrits : une seconde écriture s'appliquerait avant la première
    /// utilisation (voir en tête), elle est refusée.
    std::set<const Buffer*> m_written;
};

} // namespace levain::gpu::webgpu
