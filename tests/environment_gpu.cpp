// L'HDRI converti en cubemap sur le GPU, sur Vulkan ou sur le backend WebGPU (Dawn). L'image de
// test porte dans chaque pixel la direction qu'il regarde : chaque texel de la cubemap doit alors
// valoir sa propre direction, à tous les niveaux de mip. Une face mal orientée ou une formule
// fausse (cubeFromEquirect), une moyenne prise au mauvais endroit (downsample) s'y voient.
//   levain_environment [vulkan|webgpu]

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <format>
#include <functional>
#include <numbers>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/gtc/packing.hpp>

#include "levain/gpu/device.hpp"
#include "levain/gpu/webgpu.hpp"
#include "levain/platform/window.hpp"
#include "levain/render/environment.hpp"

namespace
{

constexpr std::uint32_t CubeSize = 16;

constexpr std::uint32_t ImageWidth = 128;
constexpr std::uint32_t ImageHeight = 64;

/// L'image équirectangulaire dont chaque pixel vaut `colorOf` de la direction de son centre.
std::vector<float> imageOf(const std::function<glm::vec3(glm::vec3)>& colorOf)
{
    const std::uint32_t width = ImageWidth;
    const std::uint32_t height = ImageHeight;
    std::vector<float> rgba;
    rgba.reserve(std::size_t{width} * height * 4);
    for (std::uint32_t y = 0; y < height; ++y)
    {
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const glm::vec2 uv{(static_cast<float>(x) + 0.5f) / static_cast<float>(width),
                               (static_cast<float>(y) + 0.5f) / static_cast<float>(height)};
            const glm::vec3 color = colorOf(levain::render::equirectDirectionOf(uv));
            rgba.insert(rgba.end(), {color.x, color.y, color.z, 1.0f});
        }
    }
    return rgba;
}

/// Le niveau `mip` de la face `face`, relu en flottants.
std::vector<glm::vec4> readFace(nvrhi::IDevice& device, nvrhi::ITexture& cube, std::uint32_t face,
                                std::uint32_t mip)
{
    const std::uint32_t size = std::max(cube.getDesc().width >> mip, 1u);
    const nvrhi::StagingTextureHandle staging =
        device.createStagingTexture(nvrhi::TextureDesc()
                                        .setWidth(size)
                                        .setHeight(size)
                                        .setFormat(levain::render::EnvironmentFormat)
                                        .setInitialState(nvrhi::ResourceStates::CopyDest)
                                        .setKeepInitialState(true),
                                    nvrhi::CpuAccessMode::Read);
    const nvrhi::CommandListHandle commandList = device.createCommandList();
    commandList->open();
    commandList->copyTexture(staging, nvrhi::TextureSlice{}, &cube,
                             nvrhi::TextureSlice().setArraySlice(face).setMipLevel(mip));
    commandList->close();
    device.executeCommandList(commandList);
    device.waitForIdle();
    std::size_t rowPitch = 0;
    const auto* mapped = static_cast<const std::byte*>(device.mapStagingTexture(
        staging, nvrhi::TextureSlice{}, nvrhi::CpuAccessMode::Read, &rowPitch));
    std::vector<glm::vec4> texels;
    if (mapped == nullptr)
    {
        return texels;
    }
    for (std::uint32_t y = 0; y < size; ++y)
    {
        const std::span row{reinterpret_cast<const std::uint64_t*>(mapped + (y * rowPitch)), size};
        for (const std::uint64_t texel : row)
        {
            texels.emplace_back(glm::unpackHalf4x16(texel));
        }
    }
    device.unmapStagingTexture(staging);
    return texels;
}

/// Le nombre de texels dont la valeur s'écarte de leur direction.
int compareWithDirections(nvrhi::IDevice& device)
{
    const std::vector<float> rgba = imageOf([](glm::vec3 direction) { return direction; });
    auto environment = levain::render::createEnvironment(
        device, {.width = ImageWidth, .height = ImageHeight, .rgba = rgba}, CubeSize);
    if (!environment)
    {
        std::println(stderr, "{}", environment.error().message);
        return -1;
    }
    int wrong = 0;
    int checked = 0;
    for (std::uint32_t mip = 0; (CubeSize >> mip) != 0; ++mip)
    {
        const std::uint32_t size = CubeSize >> mip;
        // Un dixième de texel d'écart au plus. La moyenne 2 × 2 se fait sur la face, pas sur la
        // sphère : aux niveaux grossiers, elle penche un peu vers le centre de la face (1,4° à
        // 2 × 2 texels, pour des texels de 45°). Une face retournée, elle, serait à 90°.
        const float minimumCosine =
            std::cos(0.1f * std::numbers::pi_v<float> / 2.0f / static_cast<float>(size));
        for (std::uint32_t face = 0; face < 6; ++face)
        {
            const std::vector<glm::vec4> texels = readFace(device, *environment->cube, face, mip);
            for (std::uint32_t i = 0; i < texels.size(); ++i)
            {
                const glm::vec2 uv =
                    (glm::vec2{i % size, i / size} + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
                const glm::vec3 expected = levain::render::cubeDirectionOf(face, uv);
                // Une moyenne de directions est plus courte qu'elles : seule son orientation
                // compte.
                const float cosine = glm::dot(glm::normalize(glm::vec3{texels[i]}), expected);
                wrong += cosine < minimumCosine ? 1 : 0;
                ++checked;
            }
        }
    }
    std::println("{} texels sur {} niveaux de mip ; {} hors de leur direction", checked,
                 std::bit_width(CubeSize), wrong);
    // Sans un seul texel relu, rien ne serait vérifié : le test le dit (règle n°7).
    return checked == 0 ? -1 : wrong;
}

/// La moyenne des 2 × 2 texels du centre de la face : sa valeur dans la direction de l'axe.
float centerOf(nvrhi::IDevice& device, nvrhi::ITexture& cube, std::uint32_t face, std::uint32_t mip)
{
    const std::uint32_t size = std::max(cube.getDesc().width >> mip, 1u);
    const std::vector<glm::vec4> texels = readFace(device, cube, face, mip);
    const std::uint32_t half = size / 2;
    const std::uint32_t low = half - (size > 1 ? 1 : 0);
    return (texels[(low * size) + low].x + texels[(low * size) + half].x +
            texels[(half * size) + low].x + texels[(half * size) + half].x) /
           4.0f;
}

/// Le nombre de valeurs hors de leur tolérance.
int checkConvolutions(nvrhi::IDevice& device)
{
    using levain::render::SpecularMips;
    int failures = 0;
    const auto expect =
        [&failures](std::string_view what, float value, float expected, float tolerance)
    {
        const bool near = std::abs(value - expected) <= tolerance;
        std::println("{} : {:.3f}, attendu {} ± {}{}", what, value, expected, tolerance,
                     near ? "" : " : ÉCHEC");
        failures += near ? 0 : 1;
    };

    const std::vector<float> uniform = imageOf([](glm::vec3) { return glm::vec3{2.0f}; });
    auto grey = levain::render::createEnvironment(
        device, {.width = ImageWidth, .height = ImageHeight, .rgba = uniform}, 64);
    const std::vector<float> upper =
        imageOf([](glm::vec3 direction) { return glm::vec3{direction.y > 0.0f ? 1.0f : 0.0f}; });
    auto sky = levain::render::createEnvironment(
        device, {.width = ImageWidth, .height = ImageHeight, .rgba = upper}, 64);
    if (!grey || !sky)
    {
        std::println(stderr, "{}", !grey ? grey.error().message : sky.error().message);
        return -1;
    }
    expect("ciel uniforme, irradiance", centerOf(device, *grey->irradiance, 4, 0), 2.0f, 0.01f);
    for (std::uint32_t mip = 0; mip < SpecularMips; ++mip)
    {
        expect(std::format("ciel uniforme, reflet au mip {}", mip),
               centerOf(device, *grey->specular, 1, mip), 2.0f, 0.01f);
    }
    // Faces 2 et 3 : +Y et −Y ; face 0 : +X, à l'horizon.
    expect("demi-ciel, irradiance vers le zénith", centerOf(device, *sky->irradiance, 2, 0), 1.0f,
           0.03f);
    expect("demi-ciel, irradiance vers le nadir", centerOf(device, *sky->irradiance, 3, 0), 0.0f,
           0.03f);
    expect("demi-ciel, irradiance à l'horizon", centerOf(device, *sky->irradiance, 0, 0), 0.5f,
           0.05f);
    expect("demi-ciel, reflet lisse vers le nadir", centerOf(device, *sky->specular, 3, 0), 0.0f,
           0.01f);
    // Le lobe GGX est symétrique autour de la normale : l'horizon le coupe en deux moitiés.
    expect("demi-ciel, reflet rugueux à l'horizon",
           centerOf(device, *sky->specular, 0, SpecularMips - 1), 0.5f, 0.05f);

    // En x le cosinus de vue, en y la rugosité. La somme des deux canaux : la part de la lumière
    // renvoyée quand F0 vaut 1.
    using levain::render::BrdfLutSize;
    const std::vector<glm::vec4> lut = readFace(device, *sky->brdfLut, 0, 0);
    const auto albedoOf = [&lut](std::uint32_t x, std::uint32_t y)
    { return lut[(y * BrdfLutSize) + x].x + lut[(y * BrdfLutSize) + x].y; };
    expect("BRDF lisse, vue de face", albedoOf(BrdfLutSize - 1, 0), 1.0f, 0.02f);
    // Presque lisse, la surface renvoie tout (1,000 deux fois de suite, en flottants 16 bits) : la
    // part ne doit jamais remonter, et la plus rugueuse doit avoir perdu plus du tiers.
    std::string profile;
    const float smooth = albedoOf(BrdfLutSize - 1, 0);
    float previous = smooth;
    int rising = 0;
    for (std::uint32_t y = 16; y < BrdfLutSize; y += 16)
    {
        const float albedo = albedoOf(BrdfLutSize - 1, y);
        rising += albedo > previous + 1e-3f ? 1 : 0;
        profile += std::format(" {:.3f}", albedo);
        previous = albedo;
    }
    const bool decreasing = rising == 0 && previous < smooth * (2.0f / 3.0f);
    std::println("BRDF vue de face, de rugosité en rugosité :{}{}", profile,
                 decreasing ? "" : " : ÉCHEC, elle devrait décroître");
    failures += decreasing ? 0 : 1;
    return failures;
}

int run(std::string_view backend)
{
    if (backend == "webgpu")
    {
        auto device = levain::gpu::createWebGpuDevice({.enableValidation = true});
        if (!device)
        {
            std::println(stderr, "{}", device.error().message);
            return 1;
        }
        return compareWithDirections(**device) == 0 && checkConvolutions(**device) == 0 ? 0 : 1;
    }
    auto window = levain::platform::createWindow("Levain - environnement", 64, 64);
    if (!window)
    {
        std::println(stderr, "{}", window.error().message);
        return 1;
    }
    auto gpu = levain::gpu::createGpuDevice(*window, {.enableValidation = true});
    if (!gpu)
    {
        std::println(stderr, "{}", gpu.error().message);
        return 1;
    }
    return compareWithDirections(*gpu->nvrhi) == 0 && checkConvolutions(*gpu->nvrhi) == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        const std::span arguments{argv, static_cast<std::size_t>(argc)};
        const std::string_view backend = arguments.size() == 2 ? arguments[1] : "vulkan";
        if (arguments.size() > 2 || (backend != "vulkan" && backend != "webgpu"))
        {
            std::println(stderr, "usage : levain_environment [vulkan|webgpu]");
            return 2;
        }
        return run(backend);
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
