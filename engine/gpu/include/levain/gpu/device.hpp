#pragma once

#include <memory>

#include <nvrhi/nvrhi.h>

#include "levain/core/error.hpp"
#include "levain/platform/window.hpp"

namespace levain::gpu
{

/// Ce que NVRHI ne crée pas lui-même : instance, surface et device Vulkan. Défini dans
/// `device_vk.cpp` : ni Vulkan ni vk-bootstrap n'apparaissent dans cet en-tête.
struct VulkanContext;

/// Détruit device, surface et instance, dans cet ordre.
struct VulkanContextDeleter
{
    void operator()(VulkanContext* context) const noexcept;
};

/// La swapchain, ses images enveloppées en textures NVRHI, et la cadence des frames. Définie dans
/// `swapchain_vk.cpp`.
struct Swapchain;

/// Attend que le GPU ait fini, puis détruit images, sémaphores et swapchain.
struct SwapchainDeleter
{
    void operator()(Swapchain* swapchain) const noexcept;
};

struct DeviceOptions
{
    /// Couches de validation Vulkan et couche de validation NVRHI, exigées en Debug (règle n°4).
    /// Une erreur de l'une ou de l'autre arrête le programme sur une assertion.
    bool enableValidation = false;
    /// Vulkan, ou WebGPU sur Dawn (ADR-0023) : ce dernier sert à développer et vérifier le backend
    /// WebGPU sans navigateur. Il dessine hors écran : la fenêtre reste vide, `--capture` montre
    /// l'image.
    nvrhi::GraphicsAPI api = nvrhi::GraphicsAPI::VULKAN;
};

/// Le GPU vu par le moteur : un `nvrhi::IDevice`, les objets Vulkan qui le portent, et la
/// swapchain où il dessine.
///
/// **L'ordre des membres est l'ordre de destruction inverse** : `swapchain`, puis `nvrhi`, puis
/// `vulkan`. Les images de la swapchain sont des textures NVRHI, qui doivent disparaître avant le
/// device NVRHI ; lui-même doit disparaître avant le VkDevice sur lequel il libère ses ressources.
/// Pour la même raison, ne gardez pas de `nvrhi::DeviceHandle` plus longtemps que le `GpuDevice`.
struct GpuDevice
{
    std::unique_ptr<VulkanContext, VulkanContextDeleter> vulkan;
    nvrhi::DeviceHandle nvrhi;
    std::unique_ptr<Swapchain, SwapchainDeleter> swapchain;
    /// Sans swapchain (WebGPU en natif), l'image où dessiner, à la taille de la fenêtre.
    nvrhi::TextureHandle offscreen;
};

/// Crée le device Vulkan sur le GPU le plus adapté (discret de préférence), puis le device NVRHI
/// par-dessus. Le nom du GPU et la version du pilote sont journalisés dans la catégorie `gpu`.
///
/// Échoue sans GPU compatible (Vulkan 1.3, dynamicRendering, synchronization2, timeline
/// semaphores), ou si la validation est demandée sans que ses couches soient installées.
[[nodiscard]] core::Result<GpuDevice> createGpuDevice(const platform::Window& window,
                                                      const DeviceOptions& options);

/// Le format des images de la swapchain : les pipelines qui y dessinent en ont besoin à leur
/// création.
[[nodiscard]] nvrhi::Format swapchainFormat(const GpuDevice& gpu);

/// Commence une frame : l'image de la swapchain où dessiner, ou `nullptr` si cette frame est à
/// sauter (fenêtre de taille nulle, swapchain en cours de reconstruction). La swapchain est
/// reconstruite ici dès que la taille de la fenêtre change.
[[nodiscard]] nvrhi::ITexture* beginFrame(GpuDevice& gpu, const platform::Window& window);

/// Présente l'image rendue par la frame commencée avec `beginFrame`. Avec la présentation calée sur
/// l'écran (FIFO), c'est ici que la boucle attend : elle tourne au rythme du rafraîchissement.
void presentFrame(GpuDevice& gpu);

} // namespace levain::gpu
