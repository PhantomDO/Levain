#pragma once

#include <cstdint>
#include <memory>

#include <VkBootstrap.h>
#include <nvrhi/vulkan.h>

#include "native_device.hpp"

#include "levain/gpu/device.hpp"

// En-tête privé du module : partagé par device_vk.cpp et swapchain_vk.cpp, jamais installé.

namespace levain::gpu
{

/// Le NativeDevice de Vulkan.
struct VulkanContext final : NativeDevice
{
    /// Détruit device, surface et instance, dans cet ordre.
    ~VulkanContext() override;

    vkb::Instance instance;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    vkb::Device device;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    std::uint32_t graphicsQueueFamily = 0;
};

/// Crée la swapchain à la taille donnée, et en enveloppe les images en textures NVRHI.
///
/// Elle prend le device Vulkan de NVRHI et non l'enveloppe de validation : les sémaphores sont
/// propres à Vulkan, et seule l'interface `nvrhi::vulkan::IDevice` les expose (même choix que
/// Donut).
[[nodiscard]] core::Result<std::unique_ptr<Swapchain, SwapchainDeleter>>
createSwapchain(const VulkanContext& vulkan, nvrhi::vulkan::IDevice& nvrhi,
                platform::PixelSize size);

} // namespace levain::gpu
