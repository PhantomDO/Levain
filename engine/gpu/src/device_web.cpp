// Le GpuDevice dans le navigateur : WebGPU et le canvas de la fenêtre SDL (ADR-0023).

#include <string>
#include <utility>

#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>

#include "levain/gpu/device.hpp"

namespace levain::gpu
{

void requestGpuDevice(const platform::Window& window, const DeviceOptions& options,
                      const GpuDeviceCallback& onDevice)
{
    // `--gpu vulkan` ou `--gpu d3d12` dans le navigateur s'arrête ici, en disant pourquoi : le lire
    // sans le refuser lancerait WebGPU sans un mot (règle n°7). Avant la fenêtre : le refus n'en a
    // pas besoin.
    if (auto built = requireBackendBuilt(options.api); !built)
    {
        onDevice(std::unexpected{std::move(built.error())});
        return;
    }
    // Le sélecteur du canvas que SDL a pris pour la fenêtre, « #canvas » par défaut (SDL_video.h,
    // SDL_PROP_WINDOW_EMSCRIPTEN_CANVAS_ID_STRING : dièse compris, malgré son nom).
    const std::string selector =
        SDL_GetStringProperty(SDL_GetWindowProperties(window.handle.get()),
                              SDL_PROP_WINDOW_EMSCRIPTEN_CANVAS_ID_STRING, "#canvas");
    requestWebGpuDevice(
        {.enableValidation = options.enableValidation},
        [selector, onDevice](core::Result<nvrhi::DeviceHandle> device)
        {
            if (!device)
            {
                onDevice(std::unexpected{std::move(device.error())});
                return;
            }
            auto canvas = createWebGpuCanvas(**device, selector);
            if (!canvas)
            {
                onDevice(std::unexpected{std::move(canvas.error())});
                return;
            }
            onDevice(GpuDevice{.nvrhi = std::move(*device), .canvas = std::move(*canvas)});
        });
}

nvrhi::Format swapchainFormat(const GpuDevice& gpu)
{
    return canvasFormat(*gpu.canvas);
}

nvrhi::ITexture* beginFrame(GpuDevice& gpu, const platform::Window& window)
{
    const platform::PixelSize size = platform::windowPixelSize(window);
    if (size.width <= 0 || size.height <= 0)
    {
        return nullptr;
    }
    return beginCanvasFrame(*gpu.canvas, static_cast<std::uint32_t>(size.width),
                            static_cast<std::uint32_t>(size.height));
}

void presentFrame(GpuDevice&)
{
    // Rien à faire : le navigateur présente le canvas seul, quand la frame lui rend la main.
}

} // namespace levain::gpu
