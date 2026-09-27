#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <nvrhi/validation.h>

#include "../nvrhi_messages.hpp"
#include "backend.hpp"

#include "levain/core/log.hpp"
#include "levain/gpu/webgpu.hpp"

namespace levain::gpu
{

namespace
{

std::string_view viewOf(wgpu::StringView text)
{
    return {text.data, text.length};
}

} // namespace

core::Result<nvrhi::DeviceHandle> createWebGpuDevice(const WebGpuOptions& options)
{
    nvrhi::IMessageCallback* messages =
        options.messageCallback != nullptr ? options.messageCallback : &nvrhiMessages();

    // TimedWaitAny : en natif, on attend l'adaptateur et le device sur place. Le navigateur, lui,
    // ne rendra la main qu'à travers leurs callbacks (partie C).
    static const auto timedWaitAny = wgpu::InstanceFeatureName::TimedWaitAny;
    wgpu::InstanceDescriptor instanceDesc{};
    instanceDesc.requiredFeatureCount = 1;
    instanceDesc.requiredFeatures = &timedWaitAny;
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
    wgpu::AdapterInfo info;
    adapter.GetInfo(&info);
    core::log("gpu", core::LogLevel::Info, "WebGPU : {} ({})", viewOf(info.device),
              viewOf(info.description));

    // Le BC7 des textures cuites (ADR-0020), si l'adaptateur sait le lire.
    std::vector<wgpu::FeatureName> features;
    if (adapter.HasFeature(wgpu::FeatureName::TextureCompressionBC))
    {
        features.push_back(wgpu::FeatureName::TextureCompressionBC);
    }
    wgpu::Device device;
    wgpu::DeviceDescriptor deviceDesc{};
    deviceDesc.requiredFeatureCount = features.size();
    deviceDesc.requiredFeatures = features.data();
    // Une erreur de validation de WebGPU est un bug du moteur, comme une erreur Vulkan : elle suit
    // le chemin des messages de NVRHI, donc l'assertion en Debug.
    deviceDesc.SetUncapturedErrorCallback(
        [](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView message,
           nvrhi::IMessageCallback* callback)
        {
            const std::string text = std::format("WebGPU : {}", viewOf(message));
            callback->message(nvrhi::MessageSeverity::Error, text.c_str());
        },
        messages);
    // Un device perdu (pilote planté, GPU retiré) n'est pas un bug du moteur : on le dit. Sa
    // destruction normale, à la fin du programme, ne dit rien.
    deviceDesc.SetDeviceLostCallback(
        wgpu::CallbackMode::AllowSpontaneous,
        [](const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView message)
        {
            if (reason != wgpu::DeviceLostReason::Destroyed)
            {
                core::log("gpu", core::LogLevel::Error, "device WebGPU perdu : {}",
                          viewOf(message));
            }
        });
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

    nvrhi::DeviceHandle nvrhiDevice = nvrhi::DeviceHandle::Create(
        new webgpu::Device{std::move(instance), std::move(adapter), std::move(device), messages});
    if (options.enableValidation)
    {
        nvrhiDevice = nvrhi::validation::createValidationLayer(nvrhiDevice);
    }
    return nvrhiDevice;
}

} // namespace levain::gpu
