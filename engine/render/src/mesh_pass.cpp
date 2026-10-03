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
                                      const FrameBindings& frame)
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

    MeshPass pass{.vertexShader = std::move(*vertexShader),
                  .pixelShader = std::move(*pixelShader),
                  .inputLayout = std::move(inputLayout),
                  .frameLayout = frame.layout,
                  .materialLayout = std::move(materialLayout),
                  .pipeline = {}};
    pass.pipeline = createPipeline(device, pass, target);
    if (!pass.inputLayout || !pass.materialLayout || !pass.pipeline)
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

void drawMesh(nvrhi::ICommandList& commandList, const MeshPass& pass, const FrameBindings& frame,
              nvrhi::IFramebuffer& framebuffer, const Mesh& mesh, const Instances& instances,
              nvrhi::IBindingSet& material, const SceneConstants& constants)
{
    setSceneConstants(commandList, frame, constants);

    nvrhi::GraphicsState state;
    state.pipeline = pass.pipeline;
    state.framebuffer = &framebuffer;
    state.viewport.addViewportAndScissorRect(framebuffer.getFramebufferInfo().getViewport());
    // Dans l'ordre des binding layouts du pipeline : frame, puis matériau.
    state.addBindingSet(frame.bindings).addBindingSet(&material);
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
