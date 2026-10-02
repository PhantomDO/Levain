// Le backend WebGPU de NVRHI (ADR-0023, #184), en natif sur Dawn. Sans GPU, en CI, Dawn tourne sur
// lavapipe, son adaptateur de repli.

#include <string>
#include <vector>

#include <doctest/doctest.h>
#include <nvrhi/nvrhi.h>

#include "levain/core/file.hpp"
#include "levain/gpu/webgpu.hpp"
#include "levain/render/mesh_pass.hpp"
#include "levain/render/readback.hpp"
#include "levain/render/skinning.hpp"
#include "levain/render/tonemap.hpp"

namespace
{

/// Garde les messages au lieu de s'arrêter dessus : pour vérifier qu'une erreur est signalée.
class CollectMessages final : public nvrhi::IMessageCallback
{
public:
    void message(nvrhi::MessageSeverity severity, const char* text) override
    {
        if (severity >= nvrhi::MessageSeverity::Error)
        {
            errors.emplace_back(text);
        }
    }

    std::vector<std::string> errors;
};

nvrhi::DeviceHandle webGpuDevice(nvrhi::IMessageCallback* messages = nullptr)
{
    // La couche de validation de NVRHI vérifie aussi ce que le moteur demande au backend.
    auto device = levain::gpu::createWebGpuDevice(
        {.enableValidation = messages == nullptr, .messageCallback = messages});
    INFO("erreur : " << (device ? std::string{} : device.error().message));
    REQUIRE(device.has_value());
    return *device;
}

} // namespace

TEST_CASE("le device WebGPU crée buffers, textures et samplers, sous la validation de NVRHI")
{
    const nvrhi::DeviceHandle device = webGpuDevice();
    CHECK(device->getGraphicsAPI() == nvrhi::GraphicsAPI::WEBGPU);

    const nvrhi::BufferHandle buffer = device->createBuffer(
        nvrhi::BufferDesc().setByteSize(64).setIsVertexBuffer(true).setDebugName("sommets"));
    REQUIRE(buffer);
    CHECK(buffer->getDesc().byteSize == 64);

    const nvrhi::TextureHandle texture =
        device->createTexture(nvrhi::TextureDesc()
                                  .setWidth(4)
                                  .setHeight(4)
                                  .setMipLevels(3)
                                  .setFormat(nvrhi::Format::SRGBA8_UNORM)
                                  .setDebugName("texture"));
    REQUIRE(texture);
    CHECK(texture->getDesc().mipLevels == 3);

    CHECK(device->createSampler(nvrhi::SamplerDesc().setAllFilters(true).setMaxAnisotropy(16)));
}

TEST_CASE("le WGSL des shaders du moteur, produit par le build, compile sur le device WebGPU")
{
    const nvrhi::DeviceHandle device = webGpuDevice();
    for (const char* name : {"triangle.vertexMain", "triangle.fragmentMain", "mesh.vertexMain",
                             "mesh.fragmentMain", "skinning.computeMain"})
    {
        CAPTURE(name);
        const auto wgsl =
            levain::core::readFile(std::string{LEVAIN_SHADER_DIR "/"} + name + ".wgsl");
        REQUIRE(wgsl.has_value());
        const std::string entry = std::string{name}.substr(std::string{name}.find('.') + 1);
        CHECK(device->createShader(nvrhi::ShaderDesc()
                                       .setShaderType(nvrhi::ShaderType::All)
                                       .setEntryName(entry)
                                       .setDebugName(name),
                                   wgsl->data(), wgsl->size()));
    }
}

TEST_CASE("un shader WGSL invalide est refusé et signalé, sans arrêter le programme")
{
    CollectMessages messages;
    const nvrhi::DeviceHandle device = webGpuDevice(&messages);
    const std::string wgsl = "ceci n'est pas du WGSL";

    const nvrhi::ShaderHandle shader =
        device->createShader(nvrhi::ShaderDesc()
                                 .setShaderType(nvrhi::ShaderType::Vertex)
                                 .setEntryName("main")
                                 .setDebugName("invalide"),
                             wgsl.data(), wgsl.size());

    CHECK_FALSE(shader);
    REQUIRE(messages.errors.size() == 1);
    CHECK(messages.errors[0].find("invalide") != std::string::npos);
}

TEST_CASE("le backend WebGPU déclare absent ce que WebGPU n'a pas")
{
    const nvrhi::DeviceHandle device = webGpuDevice();
    CHECK_FALSE(device->queryFeatureSupport(nvrhi::Feature::RayTracingPipeline));
    CHECK_FALSE(device->queryFeatureSupport(nvrhi::Feature::Meshlets));
    CHECK_FALSE(device->queryFeatureSupport(nvrhi::Feature::VariableRateShading));
    CHECK((device->queryFormatSupport(nvrhi::Format::SRGBA8_UNORM) &
           nvrhi::FormatSupport::ShaderSample) != nvrhi::FormatSupport::None);
    // Le BC1 : ce backend ne propose que le BC7 des textures cuites (ADR-0020).
    CHECK(device->queryFormatSupport(nvrhi::Format::BC1_UNORM) == nvrhi::FormatSupport::None);
}

TEST_CASE("les passes du moteur se créent sur le backend WebGPU")
{
    const nvrhi::DeviceHandle device = webGpuDevice();
    const nvrhi::FramebufferInfo target = nvrhi::FramebufferInfo()
                                              .addColorFormat(nvrhi::Format::RGBA8_UNORM)
                                              .setDepthFormat(levain::render::DepthFormat);

    // La passe des meshes : constantes volatiles, lumières et clusters (groupe 0), groupe 1 vide,
    // matériau (groupe 2).
    auto clusters = levain::render::createLightClusterPass(*device);
    INFO("erreur : " << (clusters ? std::string{} : clusters.error().message));
    REQUIRE(clusters.has_value());
    auto meshPass = levain::render::createMeshPass(*device, target, *clusters);
    INFO("erreur : " << (meshPass ? std::string{} : meshPass.error().message));
    REQUIRE(meshPass.has_value());
    const nvrhi::TextureHandle texture =
        device->createTexture(nvrhi::TextureDesc()
                                  .setWidth(2)
                                  .setHeight(2)
                                  .setFormat(nvrhi::Format::SRGBA8_UNORM)
                                  .setDebugName("albedo"));
    const nvrhi::SamplerHandle sampler = device->createSampler(nvrhi::SamplerDesc());
    const nvrhi::CommandListHandle commandList = device->createCommandList();
    commandList->open();
    CHECK(levain::render::createMaterialBindings(
        *device, *commandList, *meshPass, {},
        levain::render::withDefaults({.baseColor = texture},
                                     levain::render::createMaterialDefaults(*device, *commandList)),
        *sampler));
    commandList->close();
    device->executeCommandList(commandList);

    // Le tonemapping : un triangle plein écran qui lit l'image HDR (M5.2).
    auto tonemap = levain::render::createTonemapPass(
        *device, nvrhi::FramebufferInfo().addColorFormat(nvrhi::Format::RGBA8_UNORM));
    INFO("erreur : " << (tonemap ? std::string{} : tonemap.error().message));
    CHECK(tonemap.has_value());

    // Le skinning : un pipeline compute et ses storage buffers.
    auto skinning = levain::render::createSkinningPass(*device);
    INFO("erreur : " << (skinning ? std::string{} : skinning.error().message));
    CHECK(skinning.has_value());
}

TEST_CASE("une texture de profondeur se compare dans un shader, sur le backend WebGPU")
{
    // Le layout de NVRHI ne dit pas qu'une texture est de profondeur ni qu'un sampler compare : le
    // backend le lit dans les ressources liées et dans le shader (backend.hpp, DepthBinding).
    const nvrhi::DeviceHandle device = webGpuDevice();
    const std::string wgsl = R"(
@binding(0) @group(0) var shadowMap : texture_depth_2d;
@binding(128) @group(0) var shadowSampler : sampler_comparison;

@vertex fn vertexMain(@builtin(vertex_index) index : u32) -> @builtin(position) vec4<f32> {
    let uv = vec2<f32>(f32((index << 1u) & 2u), f32(index & 2u));
    return vec4<f32>(uv * 2.0 - 1.0, 0.0, 1.0);
}

@fragment fn fragmentMain() -> @location(0) vec4<f32> {
    let lit = textureSampleCompare(shadowMap, shadowSampler, vec2<f32>(0.5, 0.5), 0.5);
    return vec4<f32>(lit, lit, lit, 1.0);
}
)";
    const nvrhi::ShaderHandle vertexShader = device->createShader(
        nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Vertex).setEntryName("vertexMain"),
        wgsl.data(), wgsl.size());
    const nvrhi::ShaderHandle pixelShader = device->createShader(
        nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Pixel).setEntryName("fragmentMain"),
        wgsl.data(), wgsl.size());
    REQUIRE(vertexShader);
    REQUIRE(pixelShader);

    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::Pixel;
    layoutDesc.bindings = {nvrhi::BindingLayoutItem::Texture_SRV(0),
                           nvrhi::BindingLayoutItem::Sampler(0)};
    const nvrhi::BindingLayoutHandle layout = device->createBindingLayout(layoutDesc);
    const nvrhi::TextureHandle shadowMap =
        device->createTexture(nvrhi::TextureDesc()
                                  .setWidth(4)
                                  .setHeight(4)
                                  .setFormat(nvrhi::Format::D32)
                                  .setIsRenderTarget(true)
                                  .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                  .setKeepInitialState(true)
                                  .setDebugName("profondeur"));
    const nvrhi::SamplerHandle sampler = device->createSampler(
        nvrhi::SamplerDesc().setReductionType(nvrhi::SamplerReductionType::Comparison));
    const nvrhi::BindingSetHandle bindings =
        device->createBindingSet(nvrhi::BindingSetDesc()
                                     .addItem(nvrhi::BindingSetItem::Texture_SRV(0, shadowMap))
                                     .addItem(nvrhi::BindingSetItem::Sampler(0, sampler)),
                                 layout);
    REQUIRE(bindings);

    const nvrhi::TextureHandle target =
        device->createTexture(nvrhi::TextureDesc()
                                  .setWidth(4)
                                  .setHeight(4)
                                  .setFormat(nvrhi::Format::RGBA8_UNORM)
                                  .setIsRenderTarget(true)
                                  .setInitialState(nvrhi::ResourceStates::RenderTarget)
                                  .setKeepInitialState(true)
                                  .setDebugName("résultat"));
    const nvrhi::FramebufferHandle framebuffer =
        device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(target));
    nvrhi::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.VS = vertexShader;
    pipelineDesc.PS = pixelShader;
    pipelineDesc.addBindingLayout(layout);
    pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
    pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
    const nvrhi::GraphicsPipelineHandle pipeline =
        device->createGraphicsPipeline(pipelineDesc, framebuffer->getFramebufferInfo());
    REQUIRE(pipeline);

    // 0,5 comparé à la profondeur stockée (« plus petit que ») : derrière 0,25, devant 0,75.
    for (const auto& [stored, expected] : {std::pair{0.25f, 0}, std::pair{0.75f, 255}})
    {
        const nvrhi::CommandListHandle commandList = device->createCommandList();
        commandList->open();
        commandList->clearDepthStencilTexture(shadowMap, nvrhi::AllSubresources, true, stored,
                                              false, 0);
        nvrhi::GraphicsState state;
        state.pipeline = pipeline;
        state.framebuffer = framebuffer;
        state.viewport.addViewportAndScissorRect(framebuffer->getFramebufferInfo().getViewport());
        state.addBindingSet(bindings);
        commandList->setGraphicsState(state);
        commandList->draw(nvrhi::DrawArguments().setVertexCount(3));
        const nvrhi::StagingTextureHandle staging =
            levain::render::copyForReadback(*device, *commandList, *target);
        commandList->close();
        device->executeCommandList(commandList);
        const auto image = levain::render::readBack(*device, *staging);
        REQUIRE(image.has_value());
        CAPTURE(stored);
        CHECK(static_cast<int>(image->rgba[0]) == expected);
    }
}
