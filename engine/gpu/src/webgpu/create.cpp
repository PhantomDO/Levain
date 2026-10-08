#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nvrhi/validation.h>
#ifdef _WIN32
#include <dawn/native/DawnNative.h>
#endif

#include "../nvrhi_messages.hpp"
#include "backend.hpp"

#include "levain/core/environment.hpp"
#include "levain/core/log.hpp"
#include "levain/gpu/webgpu.hpp"

namespace levain::gpu
{

namespace
{

#ifdef _WIN32
/// Le dossier du chargeur Vulkan que le pilote installe (System32), terminé par une barre : Dawn y
/// colle le nom de la DLL. Sans lui, Dawn ne cherche vulkan-1.dll qu'à côté de lui et de
/// l'exécutable, puis sans chemin, mais avec LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR, qui exige un chemin
/// complet (« Windows Error: 87 ») : il retombe alors, sans un mot, sur son backend Null, qui ne
/// dessine rien. `SystemRoot` est toujours posée par Windows ; le repli est son emplacement par
/// défaut.
std::string windowsVulkanLoaderDir(const std::optional<std::string>& systemRoot)
{
    return systemRoot.value_or("C:\\Windows") + "\\System32\\";
}
#endif

std::string_view viewOf(wgpu::StringView text)
{
    return {text.data, text.length};
}

nvrhi::IMessageCallback* messagesOf(const WebGpuOptions& options)
{
    return options.messageCallback != nullptr ? options.messageCallback : &nvrhiMessages();
}

void logAdapter(const wgpu::Adapter& adapter)
{
    wgpu::AdapterInfo info;
    adapter.GetInfo(&info);
    core::log("gpu", core::LogLevel::Info, "WebGPU : {} ({})", viewOf(info.device),
              viewOf(info.description));
}

/// Ce que le moteur demande à tout device WebGPU : le BC7 des textures cuites (ADR-0020) si
/// l'adaptateur sait le lire, les erreurs vers les messages de NVRHI, et la perte du device dans
/// le log. `features` doit vivre jusqu'à la demande du device.
wgpu::DeviceDescriptor deviceDescriptorOf(const wgpu::Adapter& adapter,
                                          nvrhi::IMessageCallback* messages,
                                          std::vector<wgpu::FeatureName>& features)
{
    if (adapter.HasFeature(wgpu::FeatureName::TextureCompressionBC))
    {
        features.push_back(wgpu::FeatureName::TextureCompressionBC);
    }
    wgpu::DeviceDescriptor desc{};
    desc.requiredFeatureCount = features.size();
    desc.requiredFeatures = features.data();
    // Une erreur de validation de WebGPU est un bug du moteur, comme une erreur Vulkan : elle suit
    // le chemin des messages de NVRHI, donc l'assertion en Debug.
    desc.SetUncapturedErrorCallback(
        [](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView message,
           nvrhi::IMessageCallback* callback)
        {
            const std::string text = std::format("WebGPU : {}", viewOf(message));
            callback->message(nvrhi::MessageSeverity::Error, text.c_str());
        },
        messages);
    // Un device perdu (pilote planté, GPU retiré) n'est pas un bug du moteur : on le dit. Sa
    // destruction normale, à la fin du programme, ne dit rien.
    desc.SetDeviceLostCallback(
        wgpu::CallbackMode::AllowSpontaneous,
        [](const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView message)
        {
            if (reason != wgpu::DeviceLostReason::Destroyed)
            {
                core::log("gpu", core::LogLevel::Error, "device WebGPU perdu : {}",
                          viewOf(message));
            }
        });
    return desc;
}

/// Le device NVRHI par-dessus le device WebGPU, sous la couche de validation si on la demande.
nvrhi::DeviceHandle wrap(wgpu::Instance instance, wgpu::Adapter adapter, wgpu::Device device,
                         nvrhi::IMessageCallback* messages, bool enableValidation)
{
    nvrhi::DeviceHandle nvrhiDevice = nvrhi::DeviceHandle::Create(
        new webgpu::Device{std::move(instance), std::move(adapter), std::move(device), messages});
    return enableValidation ? nvrhi::validation::createValidationLayer(nvrhiDevice) : nvrhiDevice;
}

} // namespace

#ifndef __EMSCRIPTEN__

core::Result<nvrhi::DeviceHandle> createWebGpuDevice(const WebGpuOptions& options)
{
    nvrhi::IMessageCallback* messages = messagesOf(options);

    // TimedWaitAny : en natif, on attend l'adaptateur et le device sur place.
    static const auto timedWaitAny = wgpu::InstanceFeatureName::TimedWaitAny;
    wgpu::InstanceDescriptor instanceDesc{};
    instanceDesc.requiredFeatureCount = 1;
    instanceDesc.requiredFeatures = &timedWaitAny;
#ifdef _WIN32
    // dawn/native/DawnNative.h, DawnInstanceDescriptor::additionalRuntimeSearchPaths.
    const std::string loaderDir = windowsVulkanLoaderDir(core::environmentVariable("SystemRoot"));
    const char* const searchPaths[] = {loaderDir.c_str()};
    dawn::native::DawnInstanceDescriptor dawnDesc{};
    dawnDesc.additionalRuntimeSearchPathsCount = 1;
    dawnDesc.additionalRuntimeSearchPaths = searchPaths;
    instanceDesc.nextInChain = &dawnDesc;
#endif
    wgpu::Instance instance = wgpu::CreateInstance(&instanceDesc);
    if (!instance)
    {
        return core::makeError(core::ErrorCode::Unsupported, "instance WebGPU refusée");
    }

    // Le GPU le plus puissant, sinon un adaptateur de repli : sans GPU, en CI, c'est lavapipe
    // (Vulkan logiciel), que Dawn ne propose qu'à qui le demande.
    wgpu::Adapter adapter;
    std::string failure;
    for (const bool fallback : {false, true})
    {
        wgpu::RequestAdapterOptions adapterOptions{};
        adapterOptions.powerPreference = wgpu::PowerPreference::HighPerformance;
        adapterOptions.forceFallbackAdapter = fallback;
        instance.WaitAny(instance.RequestAdapter(&adapterOptions, wgpu::CallbackMode::WaitAnyOnly,
                                                 [&](wgpu::RequestAdapterStatus status,
                                                     wgpu::Adapter found, wgpu::StringView message)
                                                 {
                                                     if (status ==
                                                         wgpu::RequestAdapterStatus::Success)
                                                     {
                                                         adapter = std::move(found);
                                                     }
                                                     failure = viewOf(message);
                                                 }),
                         UINT64_MAX);
        if (adapter)
        {
            break;
        }
    }
    if (!adapter)
    {
        return core::makeError(core::ErrorCode::Unsupported,
                               std::format("aucun adaptateur WebGPU : {}", failure));
    }
    logAdapter(adapter);

    std::vector<wgpu::FeatureName> features;
    const wgpu::DeviceDescriptor deviceDesc = deviceDescriptorOf(adapter, messages, features);
    wgpu::Device device;
    instance.WaitAny(adapter.RequestDevice(&deviceDesc, wgpu::CallbackMode::WaitAnyOnly,
                                           [&](wgpu::RequestDeviceStatus status, wgpu::Device found,
                                               wgpu::StringView message)
                                           {
                                               if (status == wgpu::RequestDeviceStatus::Success)
                                               {
                                                   device = std::move(found);
                                               }
                                               failure = viewOf(message);
                                           }),
                     UINT64_MAX);
    if (!device)
    {
        return core::makeError(core::ErrorCode::Unsupported,
                               std::format("device WebGPU refusé : {}", failure));
    }
    return wrap(std::move(instance), std::move(adapter), std::move(device), messages,
                options.enableValidation);
}

void requestWebGpuDevice(const WebGpuOptions& options, const WebGpuDeviceCallback& onDevice)
{
    onDevice(createWebGpuDevice(options));
}

#else

void requestWebGpuDevice(const WebGpuOptions& options, const WebGpuDeviceCallback& onDevice)
{
    // Dans le navigateur, rien ne s'attend : l'adaptateur, puis le device, arrivent par des
    // callbacks, une fois la main rendue au navigateur (ADR-0023, point 2).
    wgpu::Instance instance = wgpu::CreateInstance(nullptr);
    wgpu::RequestAdapterOptions adapterOptions{};
    adapterOptions.powerPreference = wgpu::PowerPreference::HighPerformance;
    instance.RequestAdapter(
        &adapterOptions, wgpu::CallbackMode::AllowSpontaneous,
        [instance, options, onDevice](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter,
                                      wgpu::StringView message)
        {
            if (status != wgpu::RequestAdapterStatus::Success)
            {
                onDevice(
                    core::makeError(core::ErrorCode::Unsupported,
                                    std::format("aucun adaptateur WebGPU : {}", viewOf(message))));
                return;
            }
            logAdapter(adapter);
            nvrhi::IMessageCallback* messages = messagesOf(options);
            std::vector<wgpu::FeatureName> features;
            const wgpu::DeviceDescriptor deviceDesc =
                deviceDescriptorOf(adapter, messages, features);
            adapter.RequestDevice(
                &deviceDesc, wgpu::CallbackMode::AllowSpontaneous,
                [instance, adapter, options, onDevice,
                 messages](wgpu::RequestDeviceStatus deviceStatus, wgpu::Device device,
                           wgpu::StringView deviceMessage)
                {
                    if (deviceStatus != wgpu::RequestDeviceStatus::Success)
                    {
                        onDevice(core::makeError(
                            core::ErrorCode::Unsupported,
                            std::format("device WebGPU refusé : {}", viewOf(deviceMessage))));
                        return;
                    }
                    onDevice(wrap(instance, adapter, std::move(device), messages,
                                  options.enableValidation));
                });
        });
}

#endif

} // namespace levain::gpu
