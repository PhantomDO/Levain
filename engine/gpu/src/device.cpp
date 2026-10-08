// Le GpuDevice en natif, quel que soit le backend : le choix au lancement, la frame hors écran de
// WebGPU, et la cadence des frames que les swapchains partagent.

#include "levain/gpu/device.hpp"

#include <cstdint>
#include <deque>
#include <utility>

#include "native_device.hpp"

#include "levain/core/log.hpp"
#include "levain/gpu/webgpu.hpp"

namespace levain::gpu
{

void NativeDeviceDeleter::operator()(NativeDevice* device) const noexcept
{
    delete device;
}

void SwapchainDeleter::operator()(Swapchain* swapchain) const noexcept
{
    delete swapchain;
}

namespace
{

core::Result<GpuDevice> createOffscreenWebGpuDevice(bool enableValidation)
{
    auto device = createWebGpuDevice({.enableValidation = enableValidation});
    if (!device)
    {
        return std::unexpected{std::move(device.error())};
    }
    core::log("gpu", core::LogLevel::Info, "WebGPU en natif : rendu hors écran (--capture)");
    return GpuDevice{
        .native = nullptr, .nvrhi = std::move(*device), .swapchain = nullptr, .offscreen = nullptr};
}

} // namespace

core::Result<GpuDevice> createGpuDevice(const platform::Window& window,
                                        const DeviceOptions& options)
{
    if (auto built = requireBackendBuilt(options.api); !built)
    {
        return std::unexpected{std::move(built.error())};
    }
    if (options.api == nvrhi::GraphicsAPI::WEBGPU)
    {
        return createOffscreenWebGpuDevice(options.enableValidation);
    }
#ifdef _WIN32
    if (options.api == nvrhi::GraphicsAPI::D3D12)
    {
        return createD3d12Device(window, options.enableValidation);
    }
#endif
    return createVulkanDevice(window, options.enableValidation);
}

void requestGpuDevice(const platform::Window& window, const DeviceOptions& options,
                      const GpuDeviceCallback& onDevice)
{
    onDevice(createGpuDevice(window, options));
}

void limitFramesInFlight(std::deque<nvrhi::EventQueryHandle>& framesInFlight,
                         nvrhi::IDevice& device)
{
    nvrhi::EventQueryHandle query;
    if (framesInFlight.size() >= MaxFramesInFlight)
    {
        query = framesInFlight.front();
        framesInFlight.pop_front();
        device.waitEventQuery(query);
        device.resetEventQuery(query);
    }
    else
    {
        query = device.createEventQuery();
    }
    device.setEventQuery(query, nvrhi::CommandQueue::Graphics);
    framesInFlight.push_back(query);
}

namespace
{

/// Le format de l'image hors écran : celui qu'aurait une swapchain en sRGB.
constexpr nvrhi::Format OffscreenFormat = nvrhi::Format::SRGBA8_UNORM;

/// Minimisée sous X11, la fenêtre mesure 0 × 0 : aucune swapchain ne peut avoir cette taille.
bool isEmpty(platform::PixelSize size)
{
    return size.width <= 0 || size.height <= 0;
}

/// Sans swapchain, une image hors écran à la taille de la fenêtre, recréée quand elle change.
nvrhi::ITexture* offscreenFrame(GpuDevice& gpu, platform::PixelSize size)
{
    const auto width = static_cast<std::uint32_t>(size.width);
    const auto height = static_cast<std::uint32_t>(size.height);
    if (!gpu.offscreen || gpu.offscreen->getDesc().width != width ||
        gpu.offscreen->getDesc().height != height)
    {
        gpu.offscreen =
            gpu.nvrhi->createTexture(nvrhi::TextureDesc()
                                         .setWidth(width)
                                         .setHeight(height)
                                         .setFormat(OffscreenFormat)
                                         .setIsRenderTarget(true)
                                         .setInitialState(nvrhi::ResourceStates::RenderTarget)
                                         .setKeepInitialState(true)
                                         .setDebugName("image hors écran"));
    }
    return gpu.offscreen;
}

} // namespace

nvrhi::Format swapchainFormat(const GpuDevice& gpu)
{
    return gpu.swapchain ? gpu.swapchain->format() : OffscreenFormat;
}

nvrhi::ITexture* beginFrame(GpuDevice& gpu, const platform::Window& window)
{
    const platform::PixelSize size = platform::windowPixelSize(window);
    if (isEmpty(size))
    {
        return nullptr;
    }
    return gpu.swapchain ? gpu.swapchain->acquire(size) : offscreenFrame(gpu, size);
}

void presentFrame(GpuDevice& gpu)
{
    if (!gpu.swapchain)
    {
        // Hors écran, aucune swapchain ne freine la boucle : le CPU empilerait des images plus vite
        // que le GPU ne les rend, et la fermeture attendrait qu'il les ait toutes finies. Sur
        // lavapipe, avec Sponza, c'était plus de deux minutes. Une image en vol, comme une
        // swapchain à deux images.
        gpu.nvrhi->waitForIdle();
        return;
    }
    gpu.swapchain->present();
}

} // namespace levain::gpu
