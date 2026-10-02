// L'HDRI converti en cubemap sur le GPU, sur Vulkan ou sur le backend WebGPU (Dawn). L'image de
// test porte dans chaque pixel la direction qu'il regarde : chaque texel de la cubemap doit alors
// valoir sa propre direction, à tous les niveaux de mip. Une face mal orientée ou une formule
// fausse (cubeFromEquirect), une moyenne prise au mauvais endroit (downsample) s'y voient.
//   levain_environment [vulkan|webgpu]

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <numbers>
#include <print>
#include <span>
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

/// L'image équirectangulaire dont chaque pixel vaut la direction de son centre.
std::vector<float> directionImage(std::uint32_t width, std::uint32_t height)
{
    std::vector<float> rgba;
    rgba.reserve(std::size_t{width} * height * 4);
    for (std::uint32_t y = 0; y < height; ++y)
    {
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const glm::vec2 uv{(static_cast<float>(x) + 0.5f) / static_cast<float>(width),
                               (static_cast<float>(y) + 0.5f) / static_cast<float>(height)};
            const glm::vec3 direction = levain::render::equirectDirectionOf(uv);
            rgba.insert(rgba.end(), {direction.x, direction.y, direction.z, 1.0f});
        }
    }
    return rgba;
}

/// Le niveau `mip` de la face `face`, relu en flottants.
std::vector<glm::vec3> readFace(nvrhi::IDevice& device, nvrhi::ITexture& cube, std::uint32_t face,
                                std::uint32_t mip)
{
    const std::uint32_t size = CubeSize >> mip;
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
    std::vector<glm::vec3> texels;
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
    const std::vector<float> rgba = directionImage(128, 64);
    auto environment = levain::render::createEnvironment(
        device, {.width = 128, .height = 64, .rgba = rgba}, CubeSize);
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
            const std::vector<glm::vec3> texels = readFace(device, *environment->cube, face, mip);
            for (std::uint32_t i = 0; i < texels.size(); ++i)
            {
                const glm::vec2 uv =
                    (glm::vec2{i % size, i / size} + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
                const glm::vec3 expected = levain::render::cubeDirectionOf(face, uv);
                // Une moyenne de directions est plus courte qu'elles : seule son orientation
                // compte.
                const float cosine = glm::dot(glm::normalize(texels[i]), expected);
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
        return compareWithDirections(**device) == 0 ? 0 : 1;
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
    return compareWithDirections(*gpu->nvrhi) == 0 ? 0 : 1;
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
