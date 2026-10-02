// Le backend WebGPU de NVRHI (ADR-0023, #184), en natif sur Dawn. Sans GPU, en CI, Dawn tourne sur
// lavapipe, son adaptateur de repli.

#include <string>
#include <vector>

#include <doctest/doctest.h>
#include <nvrhi/nvrhi.h>

#include "levain/core/file.hpp"
#include "levain/gpu/webgpu.hpp"
#include "levain/render/mesh_pass.hpp"
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
