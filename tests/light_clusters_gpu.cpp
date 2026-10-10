// Le tri des lumières en clusters sur le GPU (ADR-0024) comparé à sa référence CPU, sur Vulkan ou
// sur le backend WebGPU (Dawn) : mêmes comptes et mêmes listes, cluster par cluster.
//   levain_light_clusters [vulkan|webgpu]

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <print>
#include <span>
#include <string_view>
#include <vector>

#include "gpu_test_backend.hpp"

#include "levain/gpu/device.hpp"
#include "levain/gpu/webgpu.hpp"
#include "levain/platform/window.hpp"
#include "levain/render/camera.hpp"
#include "levain/render/light_clusters.hpp"

namespace
{

using levain::render::PointLight;

/// Assez de lumières pour que des clusters en retiennent plusieurs, et quelques-uns plus que
/// `MaxLightsPerCluster` : la limite est testée elle aussi.
std::vector<PointLight> testLights()
{
    std::vector<PointLight> lights;
    lights.reserve(200);
    std::uint32_t seed = 12345;
    const auto next = [&seed]
    {
        seed = (seed * 1664525u) + 1013904223u; // un générateur congruentiel : reproductible
        return static_cast<float>(seed >> 8u) / static_cast<float>(1u << 24u);
    };
    // 40 lumières serrées autour du point que regarde la caméra : les clusters de cet endroit en
    // voient plus de 32.
    for (int i = 0; i < 40; ++i)
    {
        lights.push_back(
            PointLight{.position = {next() - 0.5f, 1.0f + next() - 0.5f, next() - 0.5f},
                       .range = 2.0f + next(),
                       .color = {1.0f, 1.0f, 1.0f},
                       .intensity = 1.0f});
    }
    // Et 160 autres, éparpillées devant elle.
    for (int i = 0; i < 160; ++i)
    {
        lights.push_back(PointLight{
            .position = {(next() * 40.0f) - 20.0f, next() * 6.0f, (next() * -60.0f) + 4.0f},
            .range = 0.5f + (next() * 2.0f),
            .color = {1.0f, 1.0f, 1.0f},
            .intensity = 1.0f});
    }
    return lights;
}

levain::render::ClusterView testView()
{
    const levain::render::Camera camera{.position = {0.0f, 2.0f, 6.0f},
                                        .target = {0.0f, 1.0f, 0.0f},
                                        .verticalFovRadians = glm::radians(60.0f),
                                        .nearPlane = 0.1f,
                                        .farPlane = 100.0f};
    return {.view = levain::render::viewOf(camera),
            .projection = levain::render::projectionOf(camera, 16.0f / 9.0f),
            .nearPlane = camera.nearPlane,
            .farPlane = camera.farPlane};
}

/// Copie `source` dans un buffer lisible par le CPU, et le relit.
std::vector<std::uint32_t> readBack(nvrhi::IDevice& device, nvrhi::IBuffer& source)
{
    const std::uint64_t size = source.getDesc().byteSize;
    const nvrhi::BufferHandle readable =
        device.createBuffer(nvrhi::BufferDesc()
                                .setByteSize(size)
                                .setCpuAccess(nvrhi::CpuAccessMode::Read)
                                .setDebugName("relecture des clusters"));
    const nvrhi::CommandListHandle commandList = device.createCommandList();
    commandList->open();
    commandList->copyBuffer(readable, 0, &source, 0, size);
    commandList->close();
    device.executeCommandList(commandList);
    device.waitForIdle();
    std::vector<std::uint32_t> values(size / sizeof(std::uint32_t));
    const void* mapped = device.mapBuffer(readable, nvrhi::CpuAccessMode::Read);
    if (mapped != nullptr)
    {
        std::memcpy(values.data(), mapped, size);
        device.unmapBuffer(readable);
    }
    return values;
}

/// Le nombre de clusters où le GPU et le CPU diffèrent.
int compareWith(nvrhi::IDevice& device)
{
    auto pass = levain::render::createLightClusterPass(device);
    if (!pass)
    {
        std::println(stderr, "{}", pass.error().message);
        return -1;
    }
    const std::vector<PointLight> lights = testLights();
    const levain::render::ClusterView view = testView();
    const nvrhi::CommandListHandle commandList = device.createCommandList();
    commandList->open();
    if (auto assigned = levain::render::assignLightsToClusters(*commandList, *pass, lights, view);
        !assigned)
    {
        std::println(stderr, "{}", assigned.error().message);
        return -1;
    }
    commandList->close();
    device.executeCommandList(commandList);

    const std::vector<std::uint32_t> counts = readBack(device, *pass->lightCounts);
    const std::vector<std::uint32_t> indices = readBack(device, *pass->lightIndices);
    const auto expected = levain::render::lightsPerClusterOf(pass->grid, view, lights);

    int different = 0;
    std::uint32_t crowded = 0;
    for (std::size_t cluster = 0; cluster < expected.size(); ++cluster)
    {
        const std::vector<std::uint32_t>& reference = expected[cluster];
        const std::size_t kept =
            std::min<std::size_t>(reference.size(), levain::render::MaxLightsPerCluster);
        const std::span gpuList{indices.data() + (cluster * levain::render::MaxLightsPerCluster),
                                kept};
        if (counts[cluster] != reference.size() ||
            !std::equal(gpuList.begin(), gpuList.end(), reference.begin()))
        {
            ++different;
        }
        crowded += reference.size() > levain::render::MaxLightsPerCluster ? 1 : 0;
    }
    std::println("{} lumières, {} clusters, dont {} au-delà de {} lumières ; {} différents du CPU",
                 lights.size(), expected.size(), crowded, levain::render::MaxLightsPerCluster,
                 different);
    // Sans cluster surchargé, la limite ne serait pas vérifiée : le test le dit (règle n°7).
    return crowded == 0 ? -1 : different;
}

int run(const levain::tests::TestBackend& backend)
{
    if (backend.api == nvrhi::GraphicsAPI::WEBGPU)
    {
        auto device = levain::gpu::createWebGpuDevice({.enableValidation = true});
        if (!device)
        {
            std::println(stderr, "{}", device.error().message);
            return 1;
        }
        return compareWith(**device) == 0 ? 0 : 1;
    }
    auto window = levain::platform::createWindow("Levain - clusters", 64, 64,
                                                 levain::gpu::surfaceFor(backend.api));
    if (!window)
    {
        std::println(stderr, "{}", window.error().message);
        return 1;
    }
    auto gpu = levain::gpu::createGpuDevice(*window, levain::tests::testDeviceOptions(backend));
    if (!gpu)
    {
        std::println(stderr, "{}", gpu.error().message);
        return 1;
    }
    return compareWith(*gpu->nvrhi) == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        const std::span arguments{argv, static_cast<std::size_t>(argc)};
        const auto backend =
            levain::tests::testBackendNamed(arguments.size() == 2 ? arguments[1] : "vulkan");
        if (arguments.size() > 2 || !backend)
        {
            std::println(stderr, "usage : levain_light_clusters [vulkan|d3d12|d3d12-warp|webgpu]");
            return 2;
        }
        return run(*backend);
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
