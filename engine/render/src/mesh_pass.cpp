#include "levain/render/mesh_pass.hpp"

#include <array>
#include <cstddef>
#include <utility>

#include "shader.hpp"

namespace levain::render
{

namespace
{

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

core::Result<MeshPass> createMeshPass(nvrhi::IDevice& device, const nvrhi::FramebufferInfo& target)
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
    frameLayoutDesc.bindings = {nvrhi::BindingLayoutItem::VolatileConstantBuffer(0)};
    nvrhi::BindingLayoutHandle frameLayout = device.createBindingLayout(frameLayoutDesc);

    // Celui du matériau occupe space2, descriptor set 2 ; space1, réservé aux ressources de passe,
    // reste vide pour l'instant. NVRHI comble le trou d'un descriptor set vide
    // (createPipelineLayout, src/vulkan/vulkan-resource-bindings.cpp).
    nvrhi::BindingLayoutDesc materialLayoutDesc;
    materialLayoutDesc.visibility = nvrhi::ShaderType::Pixel;
    materialLayoutDesc.setRegisterSpaceAndDescriptorSet(2);
    materialLayoutDesc.bindings = {nvrhi::BindingLayoutItem::Texture_SRV(0),
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
    nvrhi::BindingSetHandle frameBindings = device.createBindingSet(
        nvrhi::BindingSetDesc().addItem(nvrhi::BindingSetItem::ConstantBuffer(0, sceneConstants)),
        frameLayout);

    MeshPass pass{.vertexShader = std::move(*vertexShader),
                  .pixelShader = std::move(*pixelShader),
                  .inputLayout = std::move(inputLayout),
                  .frameLayout = std::move(frameLayout),
                  .materialLayout = std::move(materialLayout),
                  .sceneConstants = std::move(sceneConstants),
                  .frameBindings = std::move(frameBindings),
                  .pipeline = {}};
    pass.pipeline = createPipeline(device, pass, target);
    if (!pass.inputLayout || !pass.frameLayout || !pass.materialLayout || !pass.sceneConstants ||
        !pass.frameBindings || !pass.pipeline)
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

nvrhi::BindingSetHandle createMaterialBindings(nvrhi::IDevice& device, const MeshPass& pass,
                                               nvrhi::ITexture& albedo, nvrhi::ISampler& sampler)
{
    return device.createBindingSet(nvrhi::BindingSetDesc()
                                       .addItem(nvrhi::BindingSetItem::Texture_SRV(0, &albedo))
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
