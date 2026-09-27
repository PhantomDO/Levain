#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_vulkan.h>
#include <VkBootstrap.h>
#include <nvrhi/validation.h>
#include <nvrhi/vulkan.h>
#include <vulkan/vulkan.hpp>

#include "nvrhi_messages.hpp"
#include "vulkan_context.hpp"

#include "levain/core/assert.hpp"
#include "levain/core/log.hpp"
#include "levain/gpu/device.hpp"
#include "levain/gpu/webgpu.hpp"

// NVRHI est compilé en bibliothèque statique : c'est à l'application de définir le dispatcher
// dynamique de Vulkan-Hpp, une seule fois dans tout le programme, puis de l'initialiser. Seule la
// version partagée de NVRHI le fait elle-même (NVRHI, src/vulkan/vulkan-device.cpp, l. 28 à 47).
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace levain::gpu
{

void VulkanContextDeleter::operator()(VulkanContext* context) const noexcept
{
    // Une création qui échoue en route laisse des étapes vides : on ne détruit que ce qui existe.
    if (context->device.device != VK_NULL_HANDLE)
    {
        vkb::destroy_device(context->device);
    }
    if (context->surface != VK_NULL_HANDLE)
    {
        vkb::destroy_surface(context->instance, context->surface);
    }
    if (context->instance.instance != VK_NULL_HANDLE)
    {
        vkb::destroy_instance(context->instance);
    }
    delete context;
}

namespace
{

core::LogLevel toLogLevel(VkDebugUtilsMessageSeverityFlagBitsEXT severity)
{
    switch (severity)
    {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
        return core::LogLevel::Error;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
        return core::LogLevel::Warning;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
        return core::LogLevel::Info;
    default:
        return core::LogLevel::Debug;
    }
}

/// Une erreur des couches de validation, par opposition aux messages du loader Vulkan : un loader
/// qui signale une couche tierce cassée (SPECS §10) n'est pas un bug du moteur. Seule une
/// assertion s'en sert, d'où [[maybe_unused]] pour le Release (skill build, GOTCHA.md).
[[maybe_unused]] bool isValidationError(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                        VkDebugUtilsMessageTypeFlagsEXT types)
{
    return severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT &&
           (types & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) != 0;
}

VKAPI_ATTR VkBool32 VKAPI_CALL onVulkanMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                               VkDebugUtilsMessageTypeFlagsEXT types,
                                               const VkDebugUtilsMessengerCallbackDataEXT* data,
                                               void* /*userData*/)
{
    core::log("vulkan", toLogLevel(severity), "{}", data->pMessage);

    // Une erreur de validation est un bug bloquant (règle n°4) : on s'arrête dessus en Debug.
    LEVAIN_ASSERT(!isValidationError(severity, types),
                  "erreur de validation Vulkan, voir ci-dessus");

    return VK_FALSE; // la spécification l'impose pour un messager de l'application
}

/// Transforme l'échec d'une étape de vk-bootstrap en erreur lisible, raisons détaillées comprises
/// (par exemple, pourquoi chaque GPU a été écarté).
template <typename T>
std::unexpected<core::Error> failedStep(std::string_view step, const vkb::Result<T>& result)
{
    std::string message = std::format("{} : {}", step, result.error().message());
    for (const std::string& reason : result.detailed_failure_reasons())
    {
        message += std::format("\n  - {}", reason);
    }
    return core::makeError(core::ErrorCode::Unsupported, std::move(message));
}

/// « AMD Radeon RX 9070 XT (RADV GFX1201) — pilote radv Mesa 26.2.3 — Vulkan 1.4.354 ». La
/// version du pilote vient de VkPhysicalDeviceDriverProperties : le champ driverVersion des
/// propriétés de base est encodé différemment par chaque constructeur.
std::string describeGpu(vk::PhysicalDevice physicalDevice)
{
    const auto chain =
        physicalDevice
            .getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceDriverProperties>();
    const vk::PhysicalDeviceProperties& properties =
        chain.get<vk::PhysicalDeviceProperties2>().properties;
    const vk::PhysicalDeviceDriverProperties& driver =
        chain.get<vk::PhysicalDeviceDriverProperties>();

    return std::format(
        "{} — pilote {} {} — Vulkan {}.{}.{}", std::string_view{properties.deviceName.data()},
        std::string_view{driver.driverName.data()}, std::string_view{driver.driverInfo.data()},
        VK_API_VERSION_MAJOR(properties.apiVersion), VK_API_VERSION_MINOR(properties.apiVersion),
        VK_API_VERSION_PATCH(properties.apiVersion));
}

} // namespace

core::Result<GpuDevice> createGpuDevice(const platform::Window& window,
                                        const DeviceOptions& options)
{
    if (options.api == nvrhi::GraphicsAPI::WEBGPU)
    {
        auto device = createWebGpuDevice({.enableValidation = options.enableValidation});
        if (!device)
        {
            return std::unexpected{std::move(device.error())};
        }
        core::log("gpu", core::LogLevel::Info, "WebGPU en natif : rendu hors écran (--capture)");
        return GpuDevice{.vulkan = nullptr,
                         .nvrhi = std::move(*device),
                         .swapchain = nullptr,
                         .offscreen = nullptr};
    }

    std::unique_ptr<VulkanContext, VulkanContextDeleter> vulkan{new VulkanContext{}};

    // SDL sait quelles extensions de surface réclame son pilote vidéo : Wayland, X11, ou
    // VK_EXT_headless_surface pour le pilote offscreen de la CI.
    std::uint32_t surfaceExtensionCount = 0;
    const char* const* surfaceExtensions = SDL_Vulkan_GetInstanceExtensions(&surfaceExtensionCount);
    if (surfaceExtensions == nullptr)
    {
        return core::makeError(
            core::ErrorCode::Unsupported,
            std::format("SDL_Vulkan_GetInstanceExtensions : {}", SDL_GetError()));
    }

    vkb::InstanceBuilder instanceBuilder;
    instanceBuilder.set_app_name("Levain").require_api_version(1, 3, 0).enable_extensions(
        surfaceExtensionCount, surfaceExtensions);
    if (options.enableValidation)
    {
        // enable et non request : sans les couches, l'instance échoue au lieu de se passer de la
        // validation sans rien dire (règle n°7).
        instanceBuilder.enable_validation_layers().set_debug_callback(onVulkanMessage);
    }

    auto instance = instanceBuilder.build();
    if (instance.matches_error(vkb::InstanceError::requested_layers_not_present))
    {
        return core::makeError(core::ErrorCode::Unsupported,
                               "couches de validation Vulkan absentes, exigées en Debug : "
                               "sudo pacman -S vulkan-validation-layers");
    }
    if (!instance)
    {
        return failedStep("instance Vulkan", instance);
    }
    vulkan->instance = instance.value();

    if (!SDL_Vulkan_CreateSurface(window.handle.get(), vulkan->instance.instance, nullptr,
                                  &vulkan->surface))
    {
        return core::makeError(core::ErrorCode::Unsupported,
                               std::format("SDL_Vulkan_CreateSurface : {}", SDL_GetError()));
    }

    // Ce que NVRHI exige du GPU : Vulkan 1.3, dynamicRendering, synchronization2 et timeline
    // semaphores (mêmes vérifications que Donut, DeviceManager_VK::pickPhysicalDevice). Le GPU
    // discret est préféré, c'est le défaut de vk-bootstrap : la machine de référence a aussi
    // l'iGPU du 7800X3D (SPECS §10).
    // shaderDrawParameters : en HLSL, SV_VertexID compte depuis 0 sans le sommet de base du draw ;
    // en Vulkan, il l'inclut. Slang compense en le soustrayant, et doit pour cela le lire.
    // samplerAnisotropy : sans elle, un sampler anisotrope est une erreur de validation. Tous les
    // GPU de bureau l'ont, lavapipe aussi.
    VkPhysicalDeviceFeatures features{};
    features.samplerAnisotropy = VK_TRUE;

    VkPhysicalDeviceVulkan11Features features11{};
    features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    features11.shaderDrawParameters = VK_TRUE;

    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.timelineSemaphore = VK_TRUE;

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.synchronization2 = VK_TRUE;
    features13.dynamicRendering = VK_TRUE;

    vkb::PhysicalDeviceSelector selector{vulkan->instance};
    auto physicalDevice = selector.set_surface(vulkan->surface)
                              .set_minimum_version(1, 3)
                              .set_required_features(features)
                              .set_required_features_11(features11)
                              .set_required_features_12(features12)
                              .set_required_features_13(features13)
                              .add_required_extension(VK_KHR_SWAPCHAIN_EXTENSION_NAME)
                              .select();
    if (!physicalDevice)
    {
        return failedStep("aucun GPU compatible", physicalDevice);
    }

    auto device = vkb::DeviceBuilder{physicalDevice.value()}.build();
    if (!device)
    {
        return failedStep("device Vulkan", device);
    }
    vulkan->device = device.value();

    // Une seule queue pour dessiner et présenter, comme sur tous les GPU de bureau. Sinon, il
    // faudrait transférer chaque image de la swapchain d'une queue à l'autre.
    const auto graphics = vulkan->device.get_queue_and_index(vkb::QueueType::graphics);
    const auto present = vulkan->device.get_queue_index(vkb::QueueType::present);
    if (!graphics || !present || present.value() != graphics.value().second)
    {
        return core::makeError(core::ErrorCode::Unsupported,
                               "aucune queue ne sait à la fois dessiner et présenter");
    }
    vulkan->graphicsQueue = graphics.value().first;
    vulkan->graphicsQueueFamily = graphics.value().second;

    VULKAN_HPP_DEFAULT_DISPATCHER.init(vk::Instance{vulkan->instance.instance},
                                       vulkan->instance.fp_vkGetInstanceProcAddr,
                                       vk::Device{vulkan->device.device});

    core::log("gpu", core::LogLevel::Info, "{}",
              describeGpu(vk::PhysicalDevice{vulkan->device.physical_device.physical_device}));

    // NVRHI se sert des extensions activées qu'on lui annonce, et seulement de celles-là.
    const std::vector<std::string> extensionNames = vulkan->device.physical_device.get_extensions();
    std::vector<const char*> extensions;
    extensions.reserve(extensionNames.size());
    for (const std::string& name : extensionNames)
    {
        extensions.push_back(name.c_str());
    }

    // {} et non une déclaration nue : transferQueue et computeQueue n'ont pas de valeur par
    // défaut, et NVRHI prendrait des valeurs indéterminées pour de vraies queues.
    nvrhi::vulkan::DeviceDesc desc{};
    desc.errorCB = &nvrhiMessages();
    desc.instance = vulkan->instance.instance;
    desc.physicalDevice = vulkan->device.physical_device.physical_device;
    desc.device = vulkan->device.device;
    desc.graphicsQueue = vulkan->graphicsQueue;
    desc.graphicsQueueIndex = static_cast<int>(vulkan->graphicsQueueFamily);
    desc.deviceExtensions = extensions.data();
    desc.numDeviceExtensions = extensions.size();

    // NVRHI donne leur debugName aux objets Vulkan par VK_EXT_debug_utils, s'il sait l'extension
    // active : les noms apparaissent alors dans RenderDoc et dans les messages de validation.
    // vk-bootstrap ne l'active qu'avec le messager de validation, et si le pilote la propose : le
    // messager existe si et seulement si l'extension est active.
    std::array<const char*, 1> debugUtils{
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME}; // NVRHI veut un const char**
    if (vulkan->instance.debug_messenger != VK_NULL_HANDLE)
    {
        desc.instanceExtensions = debugUtils.data();
        desc.numInstanceExtensions = debugUtils.size();
    }

    const nvrhi::vulkan::DeviceHandle vulkanDevice = nvrhi::vulkan::createDevice(desc);
    if (!vulkanDevice)
    {
        return core::makeError(core::ErrorCode::Unsupported,
                               "nvrhi::vulkan::createDevice a échoué");
    }

    // La couche de validation de NVRHI enveloppe le device : elle vérifie l'usage de NVRHI
    // lui-même, là où les couches Vulkan vérifient ce que NVRHI envoie au pilote.
    nvrhi::DeviceHandle nvrhiDevice = vulkanDevice;
    if (options.enableValidation)
    {
        nvrhiDevice = nvrhi::validation::createValidationLayer(vulkanDevice);
    }

    auto swapchain = createSwapchain(*vulkan, *vulkanDevice, platform::windowPixelSize(window));
    if (!swapchain)
    {
        return std::unexpected(std::move(swapchain.error()));
    }

    return GpuDevice{.vulkan = std::move(vulkan),
                     .nvrhi = std::move(nvrhiDevice),
                     .swapchain = std::move(*swapchain),
                     .offscreen = nullptr};
}

} // namespace levain::gpu
