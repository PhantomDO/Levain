// Le cube du test de fumée, dessiné dans un canvas par le backend WebGPU de NVRHI (ADR-0023, #184).
// Même texture, même rotation, même taille (64 × 64), et une cible sans sRGB comme celle du test de
// fumée : la capture du canvas se compare pixel par pixel à tests/data/cube.ppm, rendue par Vulkan
// (tools/web-smoke.sh).

#include <array>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <emscripten/emscripten.h>
#include <glm/gtc/matrix_transform.hpp>

#include "levain/assets/image.hpp"
#include "levain/gpu/webgpu.hpp"
#include "levain/render/camera.hpp"
#include "levain/render/mesh.hpp"
#include "levain/render/mesh_pass.hpp"
#include "levain/render/texture.hpp"

namespace
{

constexpr std::uint32_t ImageSize = 64;

/// La scène, créée quand le device arrive.
struct Scene
{
    nvrhi::DeviceHandle device;
    levain::gpu::WebGpuCanvasHandle canvas;
    levain::render::LightClusterPass clusters;
    levain::render::ShadowPass shadows;
    levain::render::FrameBindings frame;
    levain::render::MeshPass meshPass;
    nvrhi::TextureHandle texture;
    nvrhi::SamplerHandle sampler;
    nvrhi::BindingSetHandle material;
    levain::render::Mesh cube;
    levain::render::Instances instances;
    nvrhi::TextureHandle depth;
    nvrhi::CommandListHandle commandList;
    int frames = 0;
};

std::optional<Scene> scene;

void fail(const std::string& message)
{
    std::printf("ÉCHEC : %s\n", message.c_str());
    emscripten_set_window_title(("Levain : échec, " + message).c_str());
}

/// Ce que le test de fumée prépare dans drawScene (tests/smoke_render.cpp), à l'identique.
void createScene(nvrhi::DeviceHandle device)
{
    auto canvas = levain::gpu::createWebGpuCanvas(*device, "#canvas", false);
    if (!canvas)
    {
        fail(canvas.error().message);
        return;
    }
    const nvrhi::FramebufferInfo target = nvrhi::FramebufferInfo()
                                              .addColorFormat(levain::gpu::canvasFormat(**canvas))
                                              .setDepthFormat(levain::render::DepthFormat);
    auto clusters = levain::render::createLightClusterPass(*device);
    if (!clusters)
    {
        fail(clusters.error().message);
        return;
    }
    auto shadows = levain::render::createShadowPass(*device, 256);
    if (!shadows)
    {
        fail(shadows.error().message);
        return;
    }
    // L'ambiance du test de fumée : un ciel uniforme (pas d'HDRI dans le navigateur).
    auto environment = levain::render::createUniformEnvironment(*device, glm::vec3{0.1f});
    if (!environment)
    {
        fail(environment.error().message);
        return;
    }
    auto frame = levain::render::createFrameBindings(*device, *clusters, *shadows, *environment);
    if (!frame)
    {
        fail(frame.error().message);
        return;
    }
    auto meshPass = levain::render::createMeshPass(*device, target, *frame);
    auto image = levain::assets::loadImage("/data/rgbw-2x2.png");
    if (!meshPass || !image)
    {
        fail(meshPass ? image.error().message : meshPass.error().message);
        return;
    }
    const std::vector<levain::assets::Image> mips =
        levain::assets::buildMipChain(std::move(*image));
    std::vector<levain::render::TextureLevel> levels;
    for (const levain::assets::Image& mip : mips)
    {
        levels.push_back({.width = mip.width,
                          .height = mip.height,
                          .bytes = std::as_bytes(std::span{mip.rgba})});
    }

    Scene created{.device = device,
                  .canvas = std::move(*canvas),
                  .clusters = std::move(*clusters),
                  .shadows = std::move(*shadows),
                  .frame = std::move(*frame),
                  .meshPass = std::move(*meshPass),
                  .texture = {},
                  .sampler = levain::render::createSampler(*device, {}),
                  .material = {},
                  .cube = {},
                  .instances = {},
                  .depth = {},
                  .commandList = device->createCommandList(),
                  .frames = 0};
    const nvrhi::CommandListHandle upload = device->createCommandList();
    upload->open();
    created.texture = levain::render::createTexture(*device, *upload, levels, "rgbw");
    created.cube = levain::render::createCube(*device, *upload);
    const std::array<levain::render::InstancePose, 1> origin{};
    created.instances = levain::render::createInstances(*device, *upload, origin);
    created.material = levain::render::createMaterialBindings(
        *device, *upload, created.meshPass,
        {.baseColorFactor = glm::vec4{1.0f},
         .metallicFactor = 0.0f,
         .roughnessFactor = 0.8f,
         .normalScale = 1.0f,
         .padding = 0.0f},
        levain::render::withDefaults({.baseColor = created.texture},
                                     levain::render::createMaterialDefaults(*device, *upload)),
        *created.sampler);
    upload->close();
    device->executeCommandList(upload);
    std::ignore = levain::render::ensureDepthTexture(*device, created.depth, ImageSize, ImageSize);
    scene = std::move(created);
}

void frame()
{
    if (!scene)
    {
        return;
    }
    nvrhi::ITexture* target = levain::gpu::beginCanvasFrame(*scene->canvas, ImageSize, ImageSize);
    if (target == nullptr)
    {
        return;
    }
    const nvrhi::FramebufferHandle framebuffer = scene->device->createFramebuffer(
        nvrhi::FramebufferDesc().addColorAttachment(target).setDepthAttachment(scene->depth));
    const levain::render::SceneConstants constants{
        .viewProjection = levain::render::viewProjectionOf(levain::render::Camera{}, 1.0f),
        .model = glm::rotate(glm::mat4{1.0f}, glm::radians(35.0f), glm::vec3{1.0f, 1.0f, 0.0f}),
    };
    nvrhi::ICommandList& commandList = *scene->commandList;
    commandList.open();
    commandList.clearTextureFloat(target, nvrhi::AllSubresources,
                                  nvrhi::Color{0.0f, 0.0f, 0.0f, 1.0f});
    commandList.clearDepthStencilTexture(scene->depth, nvrhi::AllSubresources, true, 1.0f, false,
                                         0);
    // Le même éclairage que le test de fumée (tests/smoke_render.cpp) : la même référence.
    const levain::render::Camera camera;
    const levain::render::FrameLighting lighting{
        .view = levain::render::clusterViewOf(camera, 1.0f),
        .cameraPosition = camera.position,
        .sun = {.direction = {0.4f, 1.0f, 0.6f}, .color = glm::vec3{1.0f}, .intensity = 3.0f},
        .environmentIntensity = 1.0f,
        .cascades = levain::render::cascadesOf(camera, 1.0f, {0.4f, 1.0f, 0.6f}, {})};
    levain::render::clearShadows(commandList, scene->shadows);
    std::ignore =
        levain::render::assignLightsToClusters(commandList, scene->clusters, {}, lighting.view);
    levain::render::setFrameLighting(commandList, scene->frame, scene->clusters, scene->shadows,
                                     lighting);
    levain::render::drawMesh(commandList, scene->meshPass, scene->frame, *framebuffer, scene->cube,
                             scene->instances, *scene->material, constants);
    commandList.close();
    scene->device->executeCommandList(&commandList);

    // Le script de comparaison attend ce titre avant de capturer le canvas.
    if (++scene->frames == 10)
    {
        emscripten_set_window_title("Levain : cube dessiné");
        std::printf("cube dessiné\n");
    }
}

} // namespace

int main()
{
    levain::gpu::requestWebGpuDevice({.enableValidation = true},
                                     [](levain::core::Result<nvrhi::DeviceHandle> device)
                                     {
                                         if (!device)
                                         {
                                             fail(device.error().message);
                                             return;
                                         }
                                         createScene(std::move(*device));
                                     });
    emscripten_set_main_loop(frame, 0, false);
    return 0;
}
