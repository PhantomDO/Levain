#include "levain/render/mesh_pass.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "shader.hpp"

#include "levain/render/texture.hpp"

namespace levain::render
{

namespace
{

/// L'éclairage de l'image, tel que le lit `shaders/mesh.slang` : chaque `vec3` partage ses 16
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
    glm::vec3 ambient;
    std::uint32_t padding;
    glm::uvec3 clusterGrid;
    std::uint32_t padding2;
    std::array<glm::mat4, CascadeCount> cascadeViewProjection;
    glm::vec4 cascadeFarDepths;
    glm::vec4 cascadeTexelSizes;
};

static_assert(sizeof(FrameConstants) == 432, "disposition lue par shaders/mesh.slang");
static_assert(CascadeCount == 4, "un float4 par cascade dans shaders/mesh.slang");

/// Les images qu'une command list peut éclairer : une par vue (la caméra, plus tard les ombres).
constexpr std::uint32_t MaxFramesPerCommandList = 4;

/// Le pipeline des shaders de `pass`, avec ses layouts : ce que recrée le hot-reload.
nvrhi::GraphicsPipelineHandle createPipeline(nvrhi::IDevice& device, const MeshPass& pass,
                                             const nvrhi::FramebufferInfo& target)
{
    nvrhi::GraphicsPipelineDesc desc;
    desc.primType = nvrhi::PrimitiveType::TriangleList;
    desc.inputLayout = pass.inputLayout;
    desc.VS = pass.vertexShader;
    desc.PS = pass.pixelShader;
    desc.addBindingLayout(pass.frameLayout);
    desc.addBindingLayout(pass.materialLayout);
    desc.renderState.depthStencilState.depthTestEnable = true;
    desc.renderState.depthStencilState.depthWriteEnable = true;
    desc.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::Less;
    // Faces avant dans le sens trigonométrique, comme les décrit createCube.
    desc.renderState.rasterState.frontCounterClockwise = true;
    desc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::Back;
    return device.createGraphicsPipeline(desc, target);
}

} // namespace

core::Result<MeshPass> createMeshPass(nvrhi::IDevice& device, const nvrhi::FramebufferInfo& target,
                                      const LightClusterPass& lights, const ShadowPass& shadows)
{
    auto vertexShader = loadShader(device, "mesh.vertexMain", nvrhi::ShaderType::Vertex);
    auto pixelShader = loadShader(device, "mesh.fragmentMain", nvrhi::ShaderType::Pixel);
    if (!vertexShader || !pixelShader)
    {
        return std::unexpected(vertexShader ? pixelShader.error() : vertexShader.error());
    }

    // Les noms sont les sémantiques de VertexInput dans shaders/mesh.slang, dans le même ordre (le
    // backend WebGPU numérote les attributs dans cet ordre). Les cinq premiers viennent du buffer
    // des sommets (slot 0), le dernier du buffer des instances (slot 1), lu une fois par instance.
    const std::array<nvrhi::VertexAttributeDesc, 6> attributes{
        nvrhi::VertexAttributeDesc()
            .setName("POSITION")
            .setFormat(nvrhi::Format::RGB32_FLOAT)
            .setOffset(offsetof(MeshVertex, position))
            .setElementStride(sizeof(MeshVertex)),
        nvrhi::VertexAttributeDesc()
            .setName("NORMAL")
            .setFormat(nvrhi::Format::RGB32_FLOAT)
            .setOffset(offsetof(MeshVertex, normal))
            .setElementStride(sizeof(MeshVertex)),
        nvrhi::VertexAttributeDesc()
            .setName("TANGENT")
            .setFormat(nvrhi::Format::RGBA32_FLOAT)
            .setOffset(offsetof(MeshVertex, tangent))
            .setElementStride(sizeof(MeshVertex)),
        nvrhi::VertexAttributeDesc()
            .setName("COLOR")
            .setFormat(nvrhi::Format::RGB32_FLOAT)
            .setOffset(offsetof(MeshVertex, color))
            .setElementStride(sizeof(MeshVertex)),
        nvrhi::VertexAttributeDesc()
            .setName("TEXCOORD")
            .setFormat(nvrhi::Format::RG32_FLOAT)
            .setOffset(offsetof(MeshVertex, uv))
            .setElementStride(sizeof(MeshVertex)),
        nvrhi::VertexAttributeDesc()
            .setName("INSTANCE_OFFSET")
            .setFormat(nvrhi::Format::RGB32_FLOAT)
            .setBufferIndex(1)
            .setElementStride(sizeof(glm::vec3))
            .setIsInstanced(true),
    };
    nvrhi::InputLayoutHandle inputLayout =
        device.createInputLayout(attributes.data(), attributes.size(), *vertexShader);

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
                                nvrhi::BindingLayoutItem::Sampler(0)};
    nvrhi::BindingLayoutHandle frameLayout = device.createBindingLayout(frameLayoutDesc);

    // Celui du matériau occupe space2, descriptor set 2 ; space1, réservé aux ressources de passe,
    // reste vide pour l'instant. NVRHI comble le trou d'un descriptor set vide
    // (createPipelineLayout, src/vulkan/vulkan-resource-bindings.cpp).
    nvrhi::BindingLayoutDesc materialLayoutDesc;
    materialLayoutDesc.visibility = nvrhi::ShaderType::Pixel;
    materialLayoutDesc.setRegisterSpaceAndDescriptorSet(2);
    materialLayoutDesc.bindings = {
        nvrhi::BindingLayoutItem::ConstantBuffer(0), nvrhi::BindingLayoutItem::Texture_SRV(0),
        nvrhi::BindingLayoutItem::Texture_SRV(1), nvrhi::BindingLayoutItem::Texture_SRV(2),
        nvrhi::BindingLayoutItem::Sampler(0)};
    nvrhi::BindingLayoutHandle materialLayout = device.createBindingLayout(materialLayoutDesc);

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
    // Les lumières et leurs listes par cluster, telles que le tri les a écrites.
    nvrhi::BindingSetHandle frameBindings = device.createBindingSet(
        nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, sceneConstants))
            .addItem(nvrhi::BindingSetItem::ConstantBuffer(1, frameConstants))
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, lights.lights))
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(1, lights.lightCounts))
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(2, lights.lightIndices))
            .addItem(nvrhi::BindingSetItem::Texture_SRV(3, shadows.atlas))
            .addItem(nvrhi::BindingSetItem::Sampler(0, shadows.sampler)),
        frameLayout);

    MeshPass pass{.vertexShader = std::move(*vertexShader),
                  .pixelShader = std::move(*pixelShader),
                  .inputLayout = std::move(inputLayout),
                  .frameLayout = std::move(frameLayout),
                  .materialLayout = std::move(materialLayout),
                  .sceneConstants = std::move(sceneConstants),
                  .frameConstants = std::move(frameConstants),
                  .frameBindings = std::move(frameBindings),
                  .pipeline = {}};
    pass.pipeline = createPipeline(device, pass, target);
    if (!pass.inputLayout || !pass.frameLayout || !pass.materialLayout || !pass.sceneConstants ||
        !pass.frameConstants || !pass.frameBindings || !pass.pipeline)
    {
        return core::makeError(core::ErrorCode::InvalidData, "passe des meshes refusée par NVRHI");
    }
    return pass;
}

core::Result<void> reloadMeshPassShaders(nvrhi::IDevice& device, MeshPass& pass,
                                         const nvrhi::FramebufferInfo& target)
{
    auto vertexShader = loadShader(device, "mesh.vertexMain", nvrhi::ShaderType::Vertex);
    auto pixelShader = loadShader(device, "mesh.fragmentMain", nvrhi::ShaderType::Pixel);
    if (!vertexShader || !pixelShader)
    {
        return std::unexpected(vertexShader ? pixelShader.error() : vertexShader.error());
    }

    // Sur une copie : en cas d'échec, `pass` garde ses shaders et son pipeline, et le rendu
    // continue avec eux (#46). L'ancien pipeline reste vivant tant qu'une frame en vol s'en sert :
    // NVRHI garde les ressources des command lists jusqu'à leur exécution.
    MeshPass candidate = pass;
    candidate.vertexShader = std::move(*vertexShader);
    candidate.pixelShader = std::move(*pixelShader);
    candidate.pipeline = createPipeline(device, candidate, target);
    if (!candidate.pipeline)
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               "pipeline des meshes refusé par NVRHI");
    }
    pass = std::move(candidate);
    return {};
}

MaterialDefaults createMaterialDefaults(nvrhi::IDevice& device, nvrhi::ICommandList& commandList)
{
    const auto texel = [&](std::array<std::uint8_t, 4> rgba, const char* name, nvrhi::Format format)
    {
        const std::array<TextureLevel, 1> level{
            TextureLevel{.width = 1, .height = 1, .bytes = std::as_bytes(std::span{rgba})}};
        return createTexture(device, commandList, level, name, format);
    };
    return MaterialDefaults{
        .white = texel({255, 255, 255, 255}, "matériau : blanc", nvrhi::Format::SRGBA8_UNORM),
        .whiteData = texel({255, 255, 255, 255}, "matériau : rugosité-métal neutre",
                           nvrhi::Format::RGBA8_UNORM),
        // (0, 0, 1) dans l'espace tangent, rangé de [−1, 1] vers [0, 1] : 128, 128, 255.
        .flatNormal =
            texel({128, 128, 255, 255}, "matériau : normale droite", nvrhi::Format::RGBA8_UNORM),
    };
}

MaterialTextures withDefaults(MaterialTextures textures, const MaterialDefaults& defaults)
{
    textures.baseColor = textures.baseColor != nullptr ? textures.baseColor : defaults.white.Get();
    textures.metallicRoughness = textures.metallicRoughness != nullptr ? textures.metallicRoughness
                                                                       : defaults.whiteData.Get();
    textures.normal = textures.normal != nullptr ? textures.normal : defaults.flatNormal.Get();
    return textures;
}

nvrhi::BindingSetHandle
createMaterialBindings(nvrhi::IDevice& device, nvrhi::ICommandList& commandList,
                       const MeshPass& pass, const MaterialConstants& constants,
                       const MaterialTextures& textures, nvrhi::ISampler& sampler)
{
    // Un buffer par matériau, écrit une fois : le binding set le garde vivant (NVRHI compte ses
    // références).
    const nvrhi::BufferHandle buffer =
        device.createBuffer(nvrhi::BufferDesc()
                                .setByteSize(sizeof(MaterialConstants))
                                .setIsConstantBuffer(true)
                                .setInitialState(nvrhi::ResourceStates::ConstantBuffer)
                                .setKeepInitialState(true)
                                .setDebugName("constantes de matériau"));
    commandList.writeBuffer(buffer, &constants, sizeof(constants));
    return device.createBindingSet(
        nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, buffer))
            .addItem(nvrhi::BindingSetItem::Texture_SRV(0, textures.baseColor))
            .addItem(nvrhi::BindingSetItem::Texture_SRV(1, textures.metallicRoughness))
            .addItem(nvrhi::BindingSetItem::Texture_SRV(2, textures.normal))
            .addItem(nvrhi::BindingSetItem::Sampler(0, &sampler)),
        pass.materialLayout);
}

nvrhi::ITexture* ensureDepthTexture(nvrhi::IDevice& device, nvrhi::TextureHandle& depth,
                                    std::uint32_t width, std::uint32_t height)
{
    if (depth && depth->getDesc().width == width && depth->getDesc().height == height)
    {
        return depth;
    }

    nvrhi::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = DepthFormat;
    desc.isRenderTarget = true;
    desc.initialState = nvrhi::ResourceStates::DepthWrite;
    desc.keepInitialState = true;
    desc.debugName = "depth buffer";
    depth = device.createTexture(desc);
    return depth;
}

void setFrameLighting(nvrhi::ICommandList& commandList, const MeshPass& pass,
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
        .ambient = lighting.ambient,
        .padding = 0,
        .clusterGrid = {lights.grid.x, lights.grid.y, lights.grid.z},
        .padding2 = 0,
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
    commandList.writeBuffer(pass.frameConstants, &constants, sizeof(constants));
}

void drawMesh(nvrhi::ICommandList& commandList, const MeshPass& pass,
              nvrhi::IFramebuffer& framebuffer, const Mesh& mesh, const Instances& instances,
              nvrhi::IBindingSet& material, const SceneConstants& constants)
{
    commandList.writeBuffer(pass.sceneConstants, &constants, sizeof(constants));

    nvrhi::GraphicsState state;
    state.pipeline = pass.pipeline;
    state.framebuffer = &framebuffer;
    state.viewport.addViewportAndScissorRect(framebuffer.getFramebufferInfo().getViewport());
    // Dans l'ordre des binding layouts du pipeline : frame, puis matériau.
    state.addBindingSet(pass.frameBindings).addBindingSet(&material);
    // Slot, format et décalage écrits en entier : VertexBufferBinding et IndexBufferBinding ne leur
    // donnent aucune valeur par défaut.
    state.addVertexBuffer(
        nvrhi::VertexBufferBinding().setBuffer(mesh.vertexBuffer).setSlot(0).setOffset(0));
    state.addVertexBuffer(
        nvrhi::VertexBufferBinding().setBuffer(instances.offsets).setSlot(1).setOffset(0));
    state.setIndexBuffer(nvrhi::IndexBufferBinding()
                             .setBuffer(mesh.indexBuffer)
                             .setFormat(nvrhi::Format::R32_UINT)
                             .setOffset(0));
    commandList.setGraphicsState(state);

    nvrhi::DrawArguments arguments;
    arguments.vertexCount = mesh.indexCount;
    arguments.instanceCount = instances.count;
    commandList.drawIndexed(arguments);
}

} // namespace levain::render
