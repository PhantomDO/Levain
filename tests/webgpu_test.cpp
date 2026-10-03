// Le backend WebGPU de NVRHI (ADR-0023, #184), en natif sur Dawn. Sans GPU, en CI, Dawn tourne sur
// lavapipe, son adaptateur de repli.

#include <cmath>
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
    auto shadows = levain::render::createShadowPass(*device, 256);
    INFO("erreur : " << (shadows ? std::string{} : shadows.error().message));
    REQUIRE(shadows.has_value());
    auto environment = levain::render::createUniformEnvironment(*device, glm::vec3{0.1f});
    INFO("erreur : " << (environment ? std::string{} : environment.error().message));
    REQUIRE(environment.has_value());
    auto frame = levain::render::createFrameBindings(*device, *clusters, *shadows, *environment);
    INFO("erreur : " << (frame ? std::string{} : frame.error().message));
    REQUIRE(frame.has_value());
    auto meshPass = levain::render::createMeshPass(*device, target, *frame);
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
    // backend le lit dans les ressources liées et dans le shader (backend.hpp, BindingHint).
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

TEST_CASE("un compute écrit une cubemap face par face, qu'un shader relit, sur le backend WebGPU")
{
    // Le layout de NVRHI ne dit ni qu'une texture est un cube ni le format d'une storage texture :
    // le backend le déduit des ressources et du WGSL (backend.hpp, BindingHint).
    const nvrhi::DeviceHandle device = webGpuDevice();
    const std::string writeWgsl = R"(
@binding(384) @group(0) var faces : texture_storage_2d_array<rgba16float, write>;

@compute @workgroup_size(1) fn computeMain(@builtin(global_invocation_id) id : vec3<u32>) {
    let face = f32(id.z) / 5.0;
    textureStore(faces, id.xy, i32(id.z), vec4<f32>(face, 1.0 - face, 0.5, 1.0));
}
)";
    const std::string readWgsl = R"(
@binding(0) @group(0) var cube : texture_cube<f32>;
@binding(128) @group(0) var cubeSampler : sampler;

@vertex fn vertexMain(@builtin(vertex_index) index : u32) -> @builtin(position) vec4<f32> {
    let uv = vec2<f32>(f32((index << 1u) & 2u), f32(index & 2u));
    return vec4<f32>(uv * 2.0 - 1.0, 0.0, 1.0);
}

@fragment fn fragmentMain(@builtin(position) position : vec4<f32>) -> @location(0) vec4<f32> {
    // Les six faces dans l'ordre de leurs couches : +x, -x, +y, -y, +z, -z.
    var directions = array<vec3<f32>, 6>(vec3<f32>(1.0, 0.0, 0.0), vec3<f32>(-1.0, 0.0, 0.0),
                                         vec3<f32>(0.0, 1.0, 0.0), vec3<f32>(0.0, -1.0, 0.0),
                                         vec3<f32>(0.0, 0.0, 1.0), vec3<f32>(0.0, 0.0, -1.0));
    return textureSampleLevel(cube, cubeSampler, directions[u32(position.x)], 0.0);
}
)";
    const nvrhi::ShaderHandle writeShader = device->createShader(
        nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Compute).setEntryName("computeMain"),
        writeWgsl.data(), writeWgsl.size());
    const nvrhi::ShaderHandle vertexShader = device->createShader(
        nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Vertex).setEntryName("vertexMain"),
        readWgsl.data(), readWgsl.size());
    const nvrhi::ShaderHandle pixelShader = device->createShader(
        nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Pixel).setEntryName("fragmentMain"),
        readWgsl.data(), readWgsl.size());
    REQUIRE(writeShader);
    REQUIRE(vertexShader);
    REQUIRE(pixelShader);

    const nvrhi::TextureHandle cube =
        device->createTexture(nvrhi::TextureDesc()
                                  .setDimension(nvrhi::TextureDimension::TextureCube)
                                  .setWidth(4)
                                  .setHeight(4)
                                  .setArraySize(6)
                                  .setFormat(nvrhi::Format::RGBA16_FLOAT)
                                  .setIsUAV(true)
                                  .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                  .setKeepInitialState(true)
                                  .setDebugName("cube"));
    nvrhi::BindingLayoutDesc writeLayoutDesc;
    writeLayoutDesc.visibility = nvrhi::ShaderType::Compute;
    writeLayoutDesc.bindings = {nvrhi::BindingLayoutItem::Texture_UAV(0)};
    const nvrhi::BindingLayoutHandle writeLayout = device->createBindingLayout(writeLayoutDesc);
    const nvrhi::BindingSetHandle writeBindings = device->createBindingSet(
        nvrhi::BindingSetDesc().addItem(nvrhi::BindingSetItem::Texture_UAV(0, cube)), writeLayout);
    const nvrhi::ComputePipelineHandle writePipeline = device->createComputePipeline(
        nvrhi::ComputePipelineDesc().setComputeShader(writeShader).addBindingLayout(writeLayout));
    REQUIRE(writeBindings);
    REQUIRE(writePipeline);

    nvrhi::BindingLayoutDesc readLayoutDesc;
    readLayoutDesc.visibility = nvrhi::ShaderType::Pixel;
    readLayoutDesc.bindings = {nvrhi::BindingLayoutItem::Texture_SRV(0),
                               nvrhi::BindingLayoutItem::Sampler(0)};
    const nvrhi::BindingLayoutHandle readLayout = device->createBindingLayout(readLayoutDesc);
    const nvrhi::SamplerHandle sampler = device->createSampler(nvrhi::SamplerDesc());
    const nvrhi::BindingSetHandle readBindings =
        device->createBindingSet(nvrhi::BindingSetDesc()
                                     .addItem(nvrhi::BindingSetItem::Texture_SRV(0, cube))
                                     .addItem(nvrhi::BindingSetItem::Sampler(0, sampler)),
                                 readLayout);
    const nvrhi::TextureHandle target =
        device->createTexture(nvrhi::TextureDesc()
                                  .setWidth(6)
                                  .setHeight(1)
                                  .setFormat(nvrhi::Format::RGBA8_UNORM)
                                  .setIsRenderTarget(true)
                                  .setInitialState(nvrhi::ResourceStates::RenderTarget)
                                  .setKeepInitialState(true)
                                  .setDebugName("faces lues"));
    const nvrhi::FramebufferHandle framebuffer =
        device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(target));
    nvrhi::GraphicsPipelineDesc readPipelineDesc;
    readPipelineDesc.VS = vertexShader;
    readPipelineDesc.PS = pixelShader;
    readPipelineDesc.addBindingLayout(readLayout);
    readPipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
    readPipelineDesc.renderState.depthStencilState.depthTestEnable = false;
    const nvrhi::GraphicsPipelineHandle readPipeline =
        device->createGraphicsPipeline(readPipelineDesc, framebuffer->getFramebufferInfo());
    REQUIRE(readBindings);
    REQUIRE(readPipeline);

    const nvrhi::CommandListHandle commandList = device->createCommandList();
    commandList->open();
    nvrhi::ComputeState compute;
    compute.pipeline = writePipeline;
    compute.addBindingSet(writeBindings);
    commandList->setComputeState(compute);
    commandList->dispatch(4, 4, 6);
    nvrhi::GraphicsState graphics;
    graphics.pipeline = readPipeline;
    graphics.framebuffer = framebuffer;
    graphics.viewport.addViewportAndScissorRect(framebuffer->getFramebufferInfo().getViewport());
    graphics.addBindingSet(readBindings);
    commandList->setGraphicsState(graphics);
    commandList->draw(nvrhi::DrawArguments().setVertexCount(3));
    const nvrhi::StagingTextureHandle staging =
        levain::render::copyForReadback(*device, *commandList, *target);
    commandList->close();
    device->executeCommandList(commandList);
    const auto image = levain::render::readBack(*device, *staging);
    REQUIRE(image.has_value());
    for (int face = 0; face < 6; ++face)
    {
        CAPTURE(face);
        const float expected = static_cast<float>(face) / 5.0f;
        CHECK(std::abs(static_cast<float>(image->rgba[(face * 4) + 0]) - (expected * 255.0f)) <=
              2.0f);
        CHECK(std::abs(static_cast<float>(image->rgba[(face * 4) + 1]) -
                       ((1.0f - expected) * 255.0f)) <= 2.0f);
    }
}
