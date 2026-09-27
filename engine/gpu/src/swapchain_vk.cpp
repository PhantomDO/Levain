#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <format>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include <VkBootstrap.h>
#include <nvrhi/vulkan.h>

#include "vulkan_context.hpp"

#include "levain/core/error.hpp"
#include "levain/core/log.hpp"
#include "levain/gpu/device.hpp"

namespace levain::gpu
{

namespace
{

/// Au-delà, le CPU attend le GPU. Sans limite, il empilerait des frames que l'écran afficherait
/// avec d'autant plus de retard sur l'entrée du joueur.
constexpr std::size_t MaxFramesInFlight = 2;

} // namespace

struct Swapchain
{
    vkb::Device device; ///< Copie sans propriété : de quoi reconstruire la swapchain.
    vkb::DispatchTable vk;
    VkQueue queue = VK_NULL_HANDLE;
    nvrhi::vulkan::DeviceHandle nvrhi;

    vkb::Swapchain swapchain;
    std::vector<nvrhi::TextureHandle> images;
    std::vector<VkSemaphore> acquireSemaphores;
    std::vector<VkSemaphore> presentSemaphores;
    std::size_t acquireIndex = 0;
    std::uint32_t imageIndex = 0;
    bool isOutOfDate = false;

    std::deque<nvrhi::EventQueryHandle> framesInFlight;
};

void SwapchainDeleter::operator()(Swapchain* swapchain) const noexcept
{
    // La dernière frame peut encore être en cours sur le GPU.
    swapchain->nvrhi->waitForIdle();
    swapchain->images.clear();
    swapchain->framesInFlight.clear();

    for (VkSemaphore semaphore : swapchain->acquireSemaphores)
    {
        swapchain->vk.destroySemaphore(semaphore, nullptr);
    }
    for (VkSemaphore semaphore : swapchain->presentSemaphores)
    {
        swapchain->vk.destroySemaphore(semaphore, nullptr);
    }
    if (swapchain->swapchain.swapchain != VK_NULL_HANDLE)
    {
        vkb::destroy_swapchain(swapchain->swapchain);
    }
    delete swapchain;
}

namespace
{

bool isEmpty(platform::PixelSize size)
{
    return size.width <= 0 || size.height <= 0;
}

bool matchesExtent(VkExtent2D extent, platform::PixelSize size)
{
    return static_cast<int>(extent.width) == size.width &&
           static_cast<int>(extent.height) == size.height;
}

/// Les deux formats demandés à vk-bootstrap. NVRHI a sa propre énumération de formats ; il fournit
/// la conversion vers Vulkan, pas l'inverse.
nvrhi::Format toNvrhiFormat(VkFormat format)
{
    switch (format)
    {
    case VK_FORMAT_B8G8R8A8_SRGB:
        return nvrhi::Format::SBGRA8_UNORM;
    case VK_FORMAT_B8G8R8A8_UNORM:
        return nvrhi::Format::BGRA8_UNORM;
    default:
        return nvrhi::Format::UNKNOWN;
    }
}

/// Ajoute des sémaphores jusqu'à en avoir `count`. Jamais de destruction en route : le moteur de
/// présentation peut encore attendre un sémaphore, et Vulkan ne donne aucun moyen de le savoir.
/// On les garde donc tous jusqu'à la destruction de la swapchain, comme Donut.
bool growSemaphores(std::vector<VkSemaphore>& semaphores, std::size_t count,
                    const vkb::DispatchTable& vk)
{
    VkSemaphoreCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    while (semaphores.size() < count)
    {
        VkSemaphore semaphore = VK_NULL_HANDLE;
        if (vk.createSemaphore(&info, nullptr, &semaphore) != VK_SUCCESS)
        {
            return false;
        }
        semaphores.push_back(semaphore);
    }
    return true;
}

core::Result<void> rebuildSwapchain(Swapchain& swapchain, platform::PixelSize size)
{
    // Le GPU peut encore lire les anciennes images : on attend qu'il ait fini avant de les libérer.
    swapchain.nvrhi->waitForIdle();
    swapchain.images.clear();
    swapchain.framesInFlight.clear();

    // FIFO : présentation calée sur le rafraîchissement de l'écran, le seul mode que Vulkan
    // garantit partout. TRANSFER_DST : l'effacement de l'image est une copie, pas un rendu.
    // TRANSFER_SRC : l'image peut être relue, pour la capturer (render/readback.hpp).
    auto rebuilt =
        vkb::SwapchainBuilder{swapchain.device}
            .set_old_swapchain(swapchain.swapchain)
            .set_desired_extent(static_cast<std::uint32_t>(size.width),
                                static_cast<std::uint32_t>(size.height))
            .set_desired_format({VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
            .add_fallback_format({VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
            .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
            .set_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                   VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
            .build();
    if (!rebuilt)
    {
        return core::makeError(core::ErrorCode::Unsupported,
                               std::format("swapchain : {}", rebuilt.error().message()));
    }
    if (swapchain.swapchain.swapchain != VK_NULL_HANDLE)
    {
        vkb::destroy_swapchain(swapchain.swapchain);
    }
    swapchain.swapchain = rebuilt.value();
    swapchain.isOutOfDate = false;

    const nvrhi::Format format = toNvrhiFormat(swapchain.swapchain.image_format);
    auto images = swapchain.swapchain.get_images();
    if (format == nvrhi::Format::UNKNOWN || !images)
    {
        return core::makeError(core::ErrorCode::Unsupported,
                               "swapchain : format ou images inattendus");
    }

    for (VkImage image : images.value())
    {
        nvrhi::TextureDesc desc;
        desc.width = swapchain.swapchain.extent.width;
        desc.height = swapchain.swapchain.extent.height;
        desc.format = format;
        desc.debugName = "swapchain";
        desc.isRenderTarget = true;
        // État « Present » conservé : NVRHI remet l'image dans cet état à la fermeture de chaque
        // command list qui l'a touchée, prête à être présentée sans barrière écrite à la main.
        desc.initialState = nvrhi::ResourceStates::Present;
        desc.keepInitialState = true;
        swapchain.images.push_back(swapchain.nvrhi->createHandleForNativeTexture(
            nvrhi::ObjectTypes::VK_Image, nvrhi::Object(image), desc));
    }

    // Un sémaphore de présentation par image. Pour l'acquisition, au moins un de plus que de frames
    // en vol : un sémaphore n'est réutilisé qu'une fois sa frame terminée.
    const std::size_t imageCount = swapchain.images.size();
    if (!growSemaphores(swapchain.presentSemaphores, imageCount, swapchain.vk) ||
        !growSemaphores(swapchain.acquireSemaphores, std::max(imageCount, MaxFramesInFlight + 1),
                        swapchain.vk))
    {
        return core::makeError(core::ErrorCode::OutOfMemory, "swapchain : sémaphores");
    }
    return {};
}

/// Marque la fin de la frame pour le GPU, et attend la plus ancienne si trop de frames sont en vol.
void limitFramesInFlight(Swapchain& swapchain)
{
    nvrhi::EventQueryHandle query;
    if (swapchain.framesInFlight.size() >= MaxFramesInFlight)
    {
        query = swapchain.framesInFlight.front();
        swapchain.framesInFlight.pop_front();
        swapchain.nvrhi->waitEventQuery(query);
        swapchain.nvrhi->resetEventQuery(query);
    }
    else
    {
        query = swapchain.nvrhi->createEventQuery();
    }
    swapchain.nvrhi->setEventQuery(query, nvrhi::CommandQueue::Graphics);
    swapchain.framesInFlight.push_back(query);
}

} // namespace

core::Result<std::unique_ptr<Swapchain, SwapchainDeleter>>
createSwapchain(const VulkanContext& vulkan, nvrhi::vulkan::IDevice& nvrhi,
                platform::PixelSize size)
{
    std::unique_ptr<Swapchain, SwapchainDeleter> swapchain{new Swapchain{}};
    swapchain->device = vulkan.device;
    swapchain->vk = vulkan.device.make_table();
    swapchain->queue = vulkan.graphicsQueue;
    swapchain->nvrhi = &nvrhi;

    if (auto built = rebuildSwapchain(*swapchain, size); !built)
    {
        return std::unexpected(std::move(built.error()));
    }
    return swapchain;
}

namespace
{

/// Le format de l'image hors écran : celui qu'aurait une swapchain en sRGB.
constexpr nvrhi::Format OffscreenFormat = nvrhi::Format::SRGBA8_UNORM;

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
    if (!gpu.swapchain)
    {
        return OffscreenFormat;
    }
    return toNvrhiFormat(gpu.swapchain->swapchain.image_format);
}

nvrhi::ITexture* beginFrame(GpuDevice& gpu, const platform::Window& window)
{
    // Minimisée sous X11, la fenêtre mesure 0 × 0 : aucune swapchain ne peut avoir cette taille.
    const platform::PixelSize size = platform::windowPixelSize(window);
    if (isEmpty(size))
    {
        return nullptr;
    }
    if (!gpu.swapchain)
    {
        return offscreenFrame(gpu, size);
    }
    Swapchain& swapchain = *gpu.swapchain;

    // Sous Wayland, la swapchain n'est jamais déclarée périmée au redimensionnement : c'est à nous
    // de comparer sa taille à celle de la fenêtre.
    if (swapchain.isOutOfDate || !matchesExtent(swapchain.swapchain.extent, size))
    {
        if (auto rebuilt = rebuildSwapchain(swapchain, size); !rebuilt)
        {
            core::log("gpu", core::LogLevel::Error, "{}", rebuilt.error().message);
            return nullptr;
        }
    }

    const VkSemaphore acquired = swapchain.acquireSemaphores[swapchain.acquireIndex];
    const VkResult result = swapchain.vk.acquireNextImageKHR(
        swapchain.swapchain.swapchain, std::numeric_limits<std::uint64_t>::max(), acquired,
        VK_NULL_HANDLE, &swapchain.imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        swapchain.isOutOfDate = true;
        return nullptr;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        core::log("gpu", core::LogLevel::Error, "vkAcquireNextImageKHR : {}",
                  static_cast<int>(result));
        return nullptr;
    }
    // SUBOPTIMAL : l'image est acquise et utilisable ; on reconstruira à la frame suivante.
    swapchain.isOutOfDate = result == VK_SUBOPTIMAL_KHR;
    swapchain.acquireIndex = (swapchain.acquireIndex + 1) % swapchain.acquireSemaphores.size();

    // NVRHI enregistre l'attente et la soumet avec la prochaine command list.
    swapchain.nvrhi->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, acquired, 0);
    return swapchain.images[swapchain.imageIndex];
}

void presentFrame(GpuDevice& gpu)
{
    if (!gpu.swapchain)
    {
        return;
    }
    Swapchain& swapchain = *gpu.swapchain;

    // NVRHI ne signale un sémaphore qu'à la soumission suivante : une soumission vide l'envoie
    // tout de suite (Donut, DeviceManager_VK::Present).
    const VkSemaphore rendered = swapchain.presentSemaphores[swapchain.imageIndex];
    swapchain.nvrhi->queueSignalSemaphore(nvrhi::CommandQueue::Graphics, rendered, 0);
    swapchain.nvrhi->executeCommandLists(nullptr, 0);

    VkPresentInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &rendered;
    info.swapchainCount = 1;
    info.pSwapchains = &swapchain.swapchain.swapchain;
    info.pImageIndices = &swapchain.imageIndex;

    const VkResult result = swapchain.vk.queuePresentKHR(swapchain.queue, &info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        swapchain.isOutOfDate = true;
    }
    else if (result != VK_SUCCESS)
    {
        core::log("gpu", core::LogLevel::Error, "vkQueuePresentKHR : {}", static_cast<int>(result));
    }

    limitFramesInFlight(swapchain);
    swapchain.nvrhi->runGarbageCollection();
}

} // namespace levain::gpu
