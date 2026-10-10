// Test de fumée du rendu (#16, M2.1) : dessine une scène hors écran — le triangle, ou le cube avec
// son depth buffer —, relit l'image et la compare à tests/data/<scène>.ppm. En CI, il tourne sur
// lavapipe.
//
// Avec « webgpu », la même scène passe par le backend WebGPU de NVRHI, sur Dawn (ADR-0023), et doit
// donner la même image que Vulkan : c'est la preuve que le backend traduit fidèlement le moteur.
//
// Avec « d3d12-warp » (Windows), la scène passe par Direct3D 12 sur WARP, son rendu logiciel : le
// seul adaptateur d'un runner de CI (#19). Sous le pilote offscreen de SDL, sans fenêtre Win32 ni
// swapchain ; la même référence que Vulkan.
//
// Mettre à jour une référence après un changement voulu du rendu :
//   LEVAIN_UPDATE_REFERENCE=1 SDL_VIDEO_DRIVER=offscreen \
//     ./build/linux-debug/tests/levain_smoke_render cube

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "gpu_test_backend.hpp"

#include "levain/assets/image.hpp"
#include "levain/core/environment.hpp"
#include "levain/core/file.hpp"
#include "levain/gpu/device.hpp"
#include "levain/gpu/webgpu.hpp"
#include "levain/platform/window.hpp"
#include "levain/render/camera.hpp"
#include "levain/render/debug_lines.hpp"
#include "levain/render/mesh.hpp"
#include "levain/render/mesh_pass.hpp"
#include "levain/render/readback.hpp"
#include "levain/render/shadows.hpp"
#include "levain/render/texture.hpp"
#include "levain/render/triangle.hpp"

namespace
{

constexpr int ImageSize = 64;

/// La cible est en RGBA8 : quatre octets par texel, dont l'image ne garde que les trois premiers.
constexpr std::size_t BytesPerTexel = 4;

/// Deux pixels « identiques » à ±2 près par canal : l'interpolation des couleurs peut varier d'une
/// unité d'un pilote à l'autre. Un triangle absent ou déplacé change des centaines de pixels.
constexpr int ChannelTolerance = 2;

/// Les pixels qui peuvent différer, par scène. Le bord d'une ombre est là où la profondeur comparée
/// hésite d'un texel : 8 pixels sur 4 096 diffèrent entre RADV et lavapipe (la CI), sur Vulkan
/// comme sur WebGPU. Une ombre absente ou décalée en change des centaines. Vulkan ne fixe pas le
/// tracé exact d'une ligne (ses extrémités) : 1 pixel diffère entre RADV et lavapipe (Mesa 26.2),
/// alors qu'une ligne que le cube ne cache plus en change 23.
int allowedDifferentPixels(std::string_view scene)
{
    if (scene == "shadow")
    {
        return 16;
    }
    return scene == "lines" ? 4 : 0;
}

struct Image
{
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgb; ///< Trois octets par pixel, ligne par ligne.
};

/// Le format PPM binaire (P6) : un en-tête texte et les pixels bruts. Pas de dépendance, et il
/// s'ouvre dans n'importe quelle visionneuse d'images.
bool writePpm(const std::filesystem::path& path, const Image& image)
{
    std::ofstream file{path, std::ios::binary};
    file << std::format("P6\n{} {}\n255\n", image.width, image.height);
    file.write(reinterpret_cast<const char*>(image.rgb.data()),
               static_cast<std::streamsize>(image.rgb.size()));
    return static_cast<bool>(file);
}

levain::core::Result<Image> readPpm(const std::filesystem::path& path)
{
    auto bytes = levain::core::readFile(path);
    if (!bytes)
    {
        return std::unexpected(bytes.error());
    }
    const std::string header = std::format("P6\n{} {}\n255\n", ImageSize, ImageSize);
    const std::size_t expectedSize = header.size() + std::size_t{ImageSize} * ImageSize * 3;
    if (bytes->size() != expectedSize ||
        std::memcmp(bytes->data(), header.data(), header.size()) != 0)
    {
        return levain::core::makeError(
            levain::core::ErrorCode::InvalidData,
            std::format("{} : pas une image PPM {}×{}", path.string(), ImageSize, ImageSize));
    }

    Image image{.width = ImageSize, .height = ImageSize, .rgb = {}};
    const auto* pixels = reinterpret_cast<const std::uint8_t*>(bytes->data()) + header.size();
    image.rgb.assign(pixels, pixels + (expectedSize - header.size()));
    return image;
}

/// Pixels dont au moins un canal s'écarte de plus de la tolérance.
int countDifferentPixels(const Image& actual, const Image& reference)
{
    int different = 0;
    for (std::size_t i = 0; i < actual.rgb.size(); i += 3)
    {
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            if (std::abs(actual.rgb[i + channel] - reference.rgb[i + channel]) > ChannelTolerance)
            {
                ++different;
                break;
            }
        }
    }
    return different;
}

/// Enregistre le dessin de la scène `scene` (« triangle » ou « cube ») dans `framebuffer`, déjà
/// effacé.
levain::core::Result<void> drawScene(nvrhi::IDevice& device, nvrhi::ICommandList& commandList,
                                     std::string_view scene, nvrhi::IFramebuffer& framebuffer)
{
    if (scene == "triangle")
    {
        auto triangle =
            levain::render::createTrianglePass(device, framebuffer.getFramebufferInfo());
        if (!triangle)
        {
            return std::unexpected(triangle.error());
        }
        levain::render::drawTriangle(commandList, *triangle, framebuffer);
        return {};
    }

    // Le cube texturé à une rotation fixe, qui montre trois faces : une erreur de profondeur, de
    // sens des faces ou de coordonnées de texture changerait l'image.
    //
    // La texture n'est pas le damier du sandbox mais rgbw-2x2.png, agrandie sur chaque face : seul
    // le niveau 0 est lu, filtré entre quatre couleurs. Un damier réduit sur 20 pixels dépend du
    // niveau de mip choisi, que Vulkan laisse chaque pilote approcher : 152 pixels différents entre
    // RADV et lavapipe. Quatre couleurs distinctes montrent en plus une texture retournée, que la
    // symétrie du damier cachait.
    auto clusters = levain::render::createLightClusterPass(device);
    if (!clusters)
    {
        return std::unexpected(clusters.error());
    }
    // « cube » : une passe d'ombres effacée, où rien n'est dessiné. « shadow » : le cube au-dessus
    // d'un sol, sous un soleil presque vertical, son ombre dessous (M5.3).
    const bool withGround = scene == "shadow";
    auto shadows = levain::render::createShadowPass(device, 256);
    if (!shadows)
    {
        return std::unexpected(shadows.error());
    }
    // Un ciel uniforme et sombre : l'ambiance d'avant l'IBL, qui montre les faces à l'ombre.
    auto environment = levain::render::createUniformEnvironment(device, glm::vec3{0.1f});
    if (!environment)
    {
        return std::unexpected(environment.error());
    }
    auto frame = levain::render::createFrameBindings(device, *clusters, *shadows, *environment);
    if (!frame)
    {
        return std::unexpected(frame.error());
    }
    auto meshPass =
        levain::render::createMeshPass(device, framebuffer.getFramebufferInfo(), *frame);
    if (!meshPass)
    {
        return std::unexpected(meshPass.error());
    }
    auto image = levain::assets::loadImage(LEVAIN_REFERENCE_DIR "/rgbw-2x2.png");
    if (!image)
    {
        return std::unexpected(image.error());
    }
    const std::vector<levain::assets::Image> mips =
        levain::assets::buildMipChain(std::move(*image));
    std::vector<levain::render::TextureLevel> levels;
    levels.reserve(mips.size());
    for (const levain::assets::Image& mip : mips)
    {
        levels.push_back({.width = mip.width,
                          .height = mip.height,
                          .bytes = std::as_bytes(std::span{mip.rgba})});
    }
    const nvrhi::TextureHandle checker =
        levain::render::createTexture(device, commandList, levels, "rgbw");
    const nvrhi::SamplerHandle sampler = levain::render::createSampler(device, {});
    // Non métallique et assez rugueux, comme le damier du sandbox : un diffus qui montre les faces.
    const levain::render::MaterialDefaults defaults =
        levain::render::createMaterialDefaults(device, commandList);
    const nvrhi::BindingSetHandle material = levain::render::createMaterialBindings(
        device, commandList, *meshPass,
        {.baseColorFactor = glm::vec4{1.0f},
         .metallicFactor = 0.0f,
         .roughnessFactor = 0.8f,
         .normalScale = 1.0f,
         .padding = 0.0f},
        levain::render::withDefaults({.baseColor = checker}, defaults), *sampler);

    const levain::render::Mesh cube = levain::render::createCube(device, commandList);
    // « cube-instance » : la rotation de « cube » portée par la pose de l'instance et non par la
    // matrice du modèle. Même image attendue, même référence : c'est ce qui prouve que le shader
    // tourne les sommets et les normales comme le ferait la matrice.
    const glm::quat cubeRotation =
        glm::angleAxis(glm::radians(35.0f), glm::normalize(glm::vec3{1.0f, 1.0f, 0.0f}));
    const bool rotatedInstance = scene == "cube-instance";
    const std::array<levain::render::InstancePose, 1> origin{};
    const std::array<levain::render::InstancePose, 1> rotated{
        levain::render::InstancePose{.rotation = cubeRotation}};
    const levain::render::Instances instances =
        levain::render::createInstances(device, commandList, rotatedInstance ? rotated : origin);
    const glm::mat4 cubeModel =
        withGround        ? glm::scale(glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.6f, 0.0f}),
                                       glm::vec3{0.4f})
        : rotatedInstance ? glm::mat4{1.0f}
                          : glm::mat4_cast(cubeRotation);
    const levain::render::SceneConstants constants{
        .viewProjection = levain::render::viewProjectionOf(levain::render::Camera{}, 1.0f),
        .model = cubeModel,
    };
    const glm::vec3 sunDirection =
        withGround ? glm::vec3{0.3f, 1.0f, 0.2f} : glm::vec3{0.4f, 1.0f, 0.6f};
    // Un soleil de biais, sans lumière ponctuelle : le cube montre trois faces inégalement
    // éclairées, et le tri vide ses clusters.
    const levain::render::Camera camera;
    const levain::render::FrameLighting lighting{
        .view = levain::render::clusterViewOf(camera, 1.0f),
        .cameraPosition = camera.position,
        .sun = {.direction = sunDirection, .color = glm::vec3{1.0f}, .intensity = 3.0f},
        .environmentIntensity = 1.0f,
        .cascades = levain::render::cascadesOf(camera, 1.0f, sunDirection, {.resolution = 256})};
    levain::render::clearShadows(commandList, *shadows);
    const levain::render::Mesh ground =
        levain::render::createPlane(device, commandList, 3.0f, 1.0f);
    if (withGround)
    {
        for (std::uint32_t cascade = 0; cascade < levain::render::CascadeCount; ++cascade)
        {
            levain::render::drawShadowCaster(commandList, *shadows, cascade,
                                             lighting.cascades[cascade], cube, instances,
                                             cubeModel);
            levain::render::drawShadowCaster(commandList, *shadows, cascade,
                                             lighting.cascades[cascade], ground, instances,
                                             glm::mat4{1.0f});
        }
    }
    if (auto assigned =
            levain::render::assignLightsToClusters(commandList, *clusters, {}, lighting.view);
        !assigned)
    {
        return std::unexpected(assigned.error());
    }
    levain::render::setFrameLighting(commandList, *frame, *clusters, *shadows, lighting);
    levain::render::drawMesh(commandList, *meshPass, *frame, framebuffer, cube, instances,
                             *material, constants);
    if (withGround)
    {
        levain::render::drawMesh(
            commandList, *meshPass, *frame, framebuffer, ground, instances, *material,
            {.viewProjection = constants.viewProjection, .model = glm::mat4{1.0f}});
    }
    // « lines » : le cube de « cube », une ligne rouge devant lui, entière, et une verte derrière,
    // coupée là où le cube la cache. Les couleurs sont fortes : l'image HDR passe ensuite par le
    // tonemapping.
    if (scene == "lines")
    {
        auto lines = levain::render::createDebugLinesPass(device, framebuffer.getFramebufferInfo());
        if (!lines)
        {
            return std::unexpected(lines.error());
        }
        const std::array<levain::render::DebugLine, 3> segments{{
            {.from = {-1.2f, 0.3f, 1.0f}, .to = {1.2f, 0.3f, 1.0f}, .color = {8.0f, 0.0f, 0.0f}},
            {.from = {-1.2f, -0.3f, -1.0f},
             .to = {1.2f, -0.3f, -1.0f},
             .color = {0.0f, 8.0f, 0.0f}},
            {.from = {0.0f, -1.2f, 1.0f}, .to = {0.0f, 1.2f, 1.0f}, .color = {0.0f, 0.0f, 8.0f}},
        }};
        levain::render::drawDebugLines(commandList, *lines, framebuffer, constants.viewProjection,
                                       segments);
    }
    return {};
}

/// Dessine la scène dans une texture hors écran, puis la recopie dans une texture que le CPU peut
/// lire.
levain::core::Result<Image> renderScene(nvrhi::IDevice& device, std::string_view scene)
{
    // Le noir de l'effacement, annoncé à la création pour Direct3D 12 (tonemap.cpp, l'image HDR).
    const nvrhi::Color black{0.0f, 0.0f, 0.0f, 1.0f};
    nvrhi::TextureDesc targetDesc;
    targetDesc.setClearValue(black);
    targetDesc.width = ImageSize;
    targetDesc.height = ImageSize;
    targetDesc.format = nvrhi::Format::RGBA8_UNORM;
    targetDesc.isRenderTarget = true;
    targetDesc.initialState = nvrhi::ResourceStates::RenderTarget;
    targetDesc.keepInitialState = true;
    targetDesc.debugName = "cible du test de fumée";
    const nvrhi::TextureHandle target = device.createTexture(targetDesc);

    nvrhi::FramebufferDesc framebufferDesc = nvrhi::FramebufferDesc().addColorAttachment(target);
    nvrhi::TextureHandle depth;
    if (scene != "triangle")
    {
        framebufferDesc.setDepthAttachment(
            levain::render::ensureDepthTexture(device, depth, ImageSize, ImageSize));
    }
    const nvrhi::FramebufferHandle framebuffer = device.createFramebuffer(framebufferDesc);

    const nvrhi::CommandListHandle commandList = device.createCommandList();
    commandList->open();
    commandList->clearTextureFloat(target, nvrhi::AllSubresources, black);
    if (depth)
    {
        commandList->clearDepthStencilTexture(depth, nvrhi::AllSubresources, true,
                                              levain::render::FarthestDepth, false, 0);
    }
    if (auto drawn = drawScene(device, *commandList, scene, *framebuffer); !drawn)
    {
        return std::unexpected(drawn.error());
    }
    const nvrhi::StagingTextureHandle staging =
        levain::render::copyForReadback(device, *commandList, *target);
    commandList->close();
    device.executeCommandList(commandList);
    auto readback = levain::render::readBack(device, *staging);
    if (!readback)
    {
        return std::unexpected(readback.error());
    }

    // La relecture est en RGBA ; l'image de référence garde RGB.
    Image image{.width = ImageSize, .height = ImageSize, .rgb = {}};
    image.rgb.reserve(std::size_t{ImageSize} * ImageSize * 3);
    for (std::size_t texel = 0; texel < readback->rgba.size(); texel += BytesPerTexel)
    {
        image.rgb.insert(image.rgb.end(),
                         readback->rgba.begin() + static_cast<std::ptrdiff_t>(texel),
                         readback->rgba.begin() + static_cast<std::ptrdiff_t>(texel + 3));
    }
    return image;
}

/// L'image de `scene`, rendue par Vulkan ou Direct3D 12 (`createGpuDevice`, sur une fenêtre :
/// Vulkan en tire sa surface, Direct3D 12 sa swapchain, sauf sous le pilote offscreen de SDL) ou
/// par WebGPU (sans fenêtre).
levain::core::Result<Image> renderWith(const levain::tests::TestBackend& backend,
                                       std::string_view scene)
{
    if (backend.api == nvrhi::GraphicsAPI::WEBGPU)
    {
        auto device = levain::gpu::createWebGpuDevice({.enableValidation = true});
        if (!device)
        {
            return std::unexpected(device.error());
        }
        return renderScene(**device, scene);
    }
    auto window = levain::platform::createWindow("Levain - test de fumée", ImageSize, ImageSize,
                                                 levain::gpu::surfaceFor(backend.api));
    if (!window)
    {
        return std::unexpected(window.error());
    }
    auto gpu = levain::gpu::createGpuDevice(*window, levain::tests::testDeviceOptions(backend));
    if (!gpu)
    {
        return std::unexpected(gpu.error());
    }
    return renderScene(*gpu->nvrhi, scene);
}

int runSmokeTest(std::string_view scene, std::string_view backendName,
                 const levain::tests::TestBackend& backend)
{
    auto actual = renderWith(backend, scene);
    if (!actual)
    {
        std::println(stderr, "{}", actual.error().message);
        return 1;
    }

    // « cube-instance » se compare à l'image de « cube » : c'est tout son intérêt.
    const std::string_view referenceName = scene == "cube-instance" ? "cube" : scene;
    const std::filesystem::path referencePath =
        std::filesystem::path{LEVAIN_REFERENCE_DIR} / std::format("{}.ppm", referenceName);
    const bool update = levain::core::environmentVariable("LEVAIN_UPDATE_REFERENCE").has_value();
    if (update && referenceName != scene)
    {
        std::println(stderr, "{} se compare à la référence de {} : réécrire celle-ci", scene,
                     referenceName);
        return 1;
    }
    if (update)
    {
        std::println("référence réécrite : {}", referencePath.string());
        return writePpm(referencePath, *actual) ? 0 : 1;
    }

    auto reference = readPpm(referencePath);
    if (!reference)
    {
        std::println(stderr, "{}", reference.error().message);
        return 1;
    }

    const int different = countDifferentPixels(*actual, *reference);
    std::println("{} pixels sur {} différents de la référence (tolérance ±{}, {} admis)", different,
                 ImageSize * ImageSize, ChannelTolerance, allowedDifferentPixels(scene));
    if (different <= allowedDifferentPixels(scene))
    {
        return 0;
    }

    // L'image obtenue reste à côté du binaire, pour la comparer à l'œil à la référence.
    writePpm(std::format("{}.{}.actual.ppm", scene, backendName), *actual);
    return 1;
}

} // namespace

int main(int argc, char** argv)
{
    // Tout dans le try : std::println peut lever, et une exception ne doit pas sortir de main.
    try
    {
        const std::span arguments{argv, static_cast<std::size_t>(argc)};
        const std::string_view backendName = arguments.size() == 3 ? arguments[2] : "vulkan";
        const auto backend = levain::tests::testBackendNamed(backendName);
        if (arguments.size() < 2 || arguments.size() > 3 ||
            (std::string_view{arguments[1]} != "triangle" &&
             std::string_view{arguments[1]} != "cube" &&
             std::string_view{arguments[1]} != "cube-instance" &&
             std::string_view{arguments[1]} != "lines" &&
             std::string_view{arguments[1]} != "shadow") ||
            !backend)
        {
            std::println(
                stderr,
                "usage : levain_smoke_render "
                "triangle|cube|cube-instance|lines|shadow [vulkan|d3d12|d3d12-warp|webgpu]");
            return 2;
        }
        return runSmokeTest(arguments[1], backendName, *backend);
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
