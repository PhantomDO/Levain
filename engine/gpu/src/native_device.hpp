#pragma once

#include <cstddef>
#include <deque>

#include <nvrhi/nvrhi.h>

#include "levain/core/error.hpp"
#include "levain/gpu/device.hpp"
#include "levain/platform/window.hpp"

// En-tête privé du module : ce que chaque backend natif définit, et ce qu'ils partagent. Jamais
// installé. Le backend se choisit au lancement (`DeviceOptions::api`) : un appel virtuel par frame
// le rejoint, comme le RHI d'Unreal (`FDynamicRHI`), là où NVRHI fait le reste.

namespace levain::gpu
{

/// Les objets du backend que NVRHI ne crée pas. Chaque backend en dérive (`VulkanContext`,
/// `D3d12Context`) ; son destructeur les détruit.
struct NativeDevice
{
    NativeDevice() = default;
    NativeDevice(const NativeDevice&) = delete;
    NativeDevice& operator=(const NativeDevice&) = delete;
    NativeDevice(NativeDevice&&) = delete;
    NativeDevice& operator=(NativeDevice&&) = delete;
    virtual ~NativeDevice() = default;
};

/// La swapchain d'un backend. Son destructeur attend que le GPU ait fini avant de libérer les
/// images.
struct Swapchain
{
    Swapchain() = default;
    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;
    Swapchain(Swapchain&&) = delete;
    Swapchain& operator=(Swapchain&&) = delete;
    virtual ~Swapchain() = default;

    /// Le format des images, celui des pipelines qui y dessinent.
    [[nodiscard]] virtual nvrhi::Format format() const = 0;
    /// L'image où dessiner, à la taille de la fenêtre (non nulle), ou `nullptr` si cette frame est
    /// à sauter. Reconstruit la swapchain si la taille a changé.
    [[nodiscard]] virtual nvrhi::ITexture* acquire(platform::PixelSize size) = 0;
    /// Présente l'image de `acquire`, puis cadence le CPU (`limitFramesInFlight`).
    virtual void present() = 0;
};

/// Le device Vulkan et sa swapchain (device_vk.cpp).
[[nodiscard]] core::Result<GpuDevice> createVulkanDevice(const platform::Window& window,
                                                         bool enableValidation);

#ifdef _WIN32
/// Le device Direct3D 12 et sa swapchain DXGI (device_d3d12.cpp, swapchain_d3d12.cpp).
[[nodiscard]] core::Result<GpuDevice> createD3d12Device(const platform::Window& window,
                                                        const DeviceOptions& options);
#endif

/// Au-delà, le CPU attend le GPU. Sans limite, il empilerait des frames que l'écran afficherait
/// avec d'autant plus de retard sur l'entrée du joueur.
inline constexpr std::size_t MaxFramesInFlight = 2;

/// Marque la fin de la frame pour le GPU, et attend la plus ancienne si `MaxFramesInFlight` sont
/// déjà en vol. Par des *event queries* NVRHI : la même cadence pour tous les backends.
void limitFramesInFlight(std::deque<nvrhi::EventQueryHandle>& framesInFlight,
                         nvrhi::IDevice& device);

} // namespace levain::gpu
