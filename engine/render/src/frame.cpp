#include "levain/render/frame.hpp"

#include <array>
#include <cstdint>

namespace levain::render
{

namespace
{

/// L'éclairage de l'image, tel que le lit `shaders/lighting.slang` : chaque `vec3` partage ses 16
/// octets avec le scalaire qui le suit, comme le veulent les constant buffers.
struct FrameConstants
{
    glm::mat4 view;
    glm::vec3 cameraPosition;
    float sunIntensity;
    glm::vec3 sunDirection;
    float nearPlane;
    glm::vec3 sunColor;
    float farPlane;
    float environmentIntensity;
    float padding0;
    float padding1;
    float padding2;
    glm::uvec3 clusterGrid;
    std::uint32_t padding3;
    std::array<glm::mat4, CascadeCount> cascadeViewProjection;
    glm::vec4 cascadeFarDepths;
    glm::vec4 cascadeTexelSizes;
};

static_assert(sizeof(FrameConstants) == 432, "disposition lue par shaders/lighting.slang");
static_assert(CascadeCount == 4, "un float4 par cascade dans shaders/lighting.slang");

/// Les images qu'une command list peut éclairer : une par vue (la caméra, plus tard les ombres).
constexpr std::uint32_t MaxFramesPerCommandList = 4;

} // namespace

core::Result<FrameBindings> createFrameBindings(nvrhi::IDevice& device,
                                                const LightClusterPass& lights,
                                                const ShadowPass& shadows,
                                                const Environment& environment)
{
    // ADR-0013 : un binding layout par fréquence de changement. Celui de la frame occupe space0,
    // qui devient le descriptor set 0 sous Vulkan.
    nvrhi::BindingLayoutDesc frameLayoutDesc;
    frameLayoutDesc.visibility = nvrhi::ShaderType::All;
    frameLayoutDesc.setRegisterSpaceAndDescriptorSet(0);
    frameLayoutDesc.bindings = {nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                                nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
                                nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0),
                                nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1),
                                nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2),
                                nvrhi::BindingLayoutItem::Texture_SRV(3),
                                nvrhi::BindingLayoutItem::Sampler(0),
                                nvrhi::BindingLayoutItem::Texture_SRV(4),
                                nvrhi::BindingLayoutItem::Texture_SRV(5),
                                nvrhi::BindingLayoutItem::Texture_SRV(6),
                                nvrhi::BindingLayoutItem::Sampler(1)};
    nvrhi::BindingLayoutHandle frameLayout = device.createBindingLayout(frameLayoutDesc);

    // Volatile : le contenu ne vit que le temps d'une command list, et NVRHI en fournit une
    // nouvelle version à chaque écriture. Pas de buffer par frame en vol à gérer (QA du
    // 2026-09-21).
    nvrhi::BufferHandle sceneConstants =
        device.createBuffer(nvrhi::BufferDesc()
                                .setByteSize(sizeof(SceneConstants))
                                .setIsConstantBuffer(true)
                                .setIsVolatile(true)
                                .setMaxVersions(MaxMeshDrawsPerCommandList)
                                .setDebugName("constantes de scène"));
    nvrhi::BufferHandle frameConstants =
        device.createBuffer(nvrhi::BufferDesc()
                                .setByteSize(sizeof(FrameConstants))
                                .setIsConstantBuffer(true)
                                .setIsVolatile(true)
                                .setMaxVersions(MaxFramesPerCommandList)
                                .setDebugName("constantes d'éclairage"));
    // Trilinéaire, étiré au bord : la table de la BRDF ne doit pas reboucler de la vue rasante à la
    // vue de face. Le binding set garde le sampler en vie.
    const nvrhi::SamplerHandle environmentSampler =
        device.createSampler(nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(
            nvrhi::SamplerAddressMode::Clamp));
    // Les lumières et leurs listes par cluster, telles que le tri les a écrites, les ombres et le
    // ciel.
    nvrhi::BindingSetHandle frameBindings = device.createBindingSet(
        nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, sceneConstants))
            .addItem(nvrhi::BindingSetItem::ConstantBuffer(1, frameConstants))
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, lights.lights))
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(1, lights.lightCounts))
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(2, lights.lightIndices))
            .addItem(nvrhi::BindingSetItem::Texture_SRV(3, shadows.atlas))
            .addItem(nvrhi::BindingSetItem::Sampler(0, shadows.sampler))
            .addItem(nvrhi::BindingSetItem::Texture_SRV(4, environment.irradiance))
            .addItem(nvrhi::BindingSetItem::Texture_SRV(5, environment.specular))
            .addItem(nvrhi::BindingSetItem::Texture_SRV(6, environment.brdfLut))
            .addItem(nvrhi::BindingSetItem::Sampler(1, environmentSampler)),
        frameLayout);
    if (!frameLayout || !sceneConstants || !frameConstants || !frameBindings)
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               "ressources de l'image refusées par NVRHI");
    }
    return FrameBindings{.layout = std::move(frameLayout),
                         .sceneConstants = std::move(sceneConstants),
                         .frameConstants = std::move(frameConstants),
                         .bindings = std::move(frameBindings)};
}

void setFrameLighting(nvrhi::ICommandList& commandList, const FrameBindings& frame,
                      const LightClusterPass& lights, const ShadowPass& shadows,
                      const FrameLighting& lighting)
{
    FrameConstants constants{
        .view = lighting.view.view,
        .cameraPosition = lighting.cameraPosition,
        .sunIntensity = lighting.sun.intensity,
        .sunDirection = glm::normalize(lighting.sun.direction),
        .nearPlane = lighting.view.nearPlane,
        .sunColor = lighting.sun.color,
        .farPlane = lighting.view.farPlane,
        .environmentIntensity = lighting.environmentIntensity,
        .padding0 = 0.0f,
        .padding1 = 0.0f,
        .padding2 = 0.0f,
        .clusterGrid = {lights.grid.x, lights.grid.y, lights.grid.z},
        .padding3 = 0,
        .cascadeViewProjection = {},
        .cascadeFarDepths = {},
        .cascadeTexelSizes = {},
    };
    for (std::uint32_t i = 0; i < CascadeCount; ++i)
    {
        const glm::mat4& viewProjection = lighting.cascades[i].viewProjection;
        constants.cascadeViewProjection[i] = viewProjection;
        constants.cascadeFarDepths[static_cast<glm::length_t>(i)] = lighting.cascades[i].farDepth;
        // La projection orthographique couvre 2 / échelle unités du monde sur `resolution` texels.
        constants.cascadeTexelSizes[static_cast<glm::length_t>(i)] =
            2.0f /
            (glm::length(glm::vec3{viewProjection[0]}) * static_cast<float>(shadows.resolution));
    }
    commandList.writeBuffer(frame.frameConstants, &constants, sizeof(constants));
}

void setSceneConstants(nvrhi::ICommandList& commandList, const FrameBindings& frame,
                       const SceneConstants& constants)
{
    commandList.writeBuffer(frame.sceneConstants, &constants, sizeof(constants));
}

} // namespace levain::render
