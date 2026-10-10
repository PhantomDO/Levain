/*
 * Adapté de Donut, src/app/dx12/DeviceManager_DX12.cpp (https://github.com/NVIDIA-RTX/Donut) :
 *
 * Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * License for glfw
 *
 * Copyright (c) 2002-2006 Marcus Geelnard
 *
 * Copyright (c) 2006-2019 Camilla Lowy
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would
 *    be appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and must not
 *    be misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source
 *    distribution.
 */

// Le device Direct3D 12 : les couches de debug de Direct3D 12 et de DXGI, la factory DXGI,
// l'adaptateur, le device et sa queue, puis le device NVRHI par-dessus et la swapchain (ADR-0035,
// M1.4). Windows seulement.

#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>
#include <nvrhi/d3d12.h>
#include <nvrhi/validation.h>

#include "d3d12_context.hpp"
#include "native_device.hpp"
#include "nvrhi_messages.hpp"

#include "levain/core/assert.hpp"
#include "levain/core/log.hpp"
#include "levain/gpu/device.hpp"

namespace levain::gpu
{

D3d12Context::~D3d12Context()
{
    if (infoQueue)
    {
        infoQueue->UnregisterMessageCallback(messageCookie);
    }
}

namespace
{

/// Le niveau de fonctionnalités exigé : celui de tous les GPU Direct3D 12 de la dernière décennie,
/// WARP compris. Nos shaders sont en shader model 6.0 (cmake/LevainShaders.cmake), vérifié à part.
constexpr D3D_FEATURE_LEVEL MinimumFeatureLevel = D3D_FEATURE_LEVEL_12_0;
constexpr D3D_SHADER_MODEL MinimumShaderModel = D3D_SHADER_MODEL_6_0;

core::LogLevel toLogLevel(D3D12_MESSAGE_SEVERITY severity)
{
    switch (severity)
    {
    case D3D12_MESSAGE_SEVERITY_CORRUPTION:
        return core::LogLevel::Critical;
    case D3D12_MESSAGE_SEVERITY_ERROR:
        return core::LogLevel::Error;
    case D3D12_MESSAGE_SEVERITY_WARNING:
        return core::LogLevel::Warning;
    case D3D12_MESSAGE_SEVERITY_INFO:
        return core::LogLevel::Info;
    default:
        return core::LogLevel::Debug;
    }
}

/// Une erreur de la couche de debug, ou pire. Seule une assertion s'en sert, d'où [[maybe_unused]]
/// pour le Release (skill build, GOTCHA.md).
[[maybe_unused]] bool isDebugLayerError(D3D12_MESSAGE_SEVERITY severity)
{
    return severity == D3D12_MESSAGE_SEVERITY_ERROR ||
           severity == D3D12_MESSAGE_SEVERITY_CORRUPTION;
}

/// Le chemin commun des messages des deux couches de debug, Direct3D 12 et DXGI : nos logs, dans la
/// catégorie `layer`, et une assertion sur une erreur ou une corruption.
void reportDebugMessage(std::string_view layer, D3D12_MESSAGE_SEVERITY severity,
                        std::string_view text)
{
    core::log(layer, toLogLevel(severity), "{}", text);

    // Une erreur de validation est un bug bloquant (règle n°4) : on s'arrête dessus en Debug.
    LEVAIN_ASSERT(!isDebugLayerError(severity), "erreur d'une couche de debug, voir ci-dessus");
}

/// Le pendant du messager de Vulkan (device_vk.cpp) : la couche de debug appelle cette fonction
/// pour chaque message, sur le fil qui a fait l'appel fautif.
void onD3d12Message(D3D12_MESSAGE_CATEGORY /*category*/, D3D12_MESSAGE_SEVERITY severity,
                    D3D12_MESSAGE_ID id, LPCSTR description, void* /*context*/)
{
    reportDebugMessage("d3d12", severity,
                       std::format("{} (D3D12_MESSAGE_ID {})", description, static_cast<int>(id)));
}

/// Les sévérités de DXGI, rangées comme celles de Direct3D 12, pour le même chemin.
D3D12_MESSAGE_SEVERITY d3d12SeverityOf(DXGI_INFO_QUEUE_MESSAGE_SEVERITY severity)
{
    switch (severity)
    {
    case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION:
        return D3D12_MESSAGE_SEVERITY_CORRUPTION;
    case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR:
        return D3D12_MESSAGE_SEVERITY_ERROR;
    case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_WARNING:
        return D3D12_MESSAGE_SEVERITY_WARNING;
    case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_INFO:
        return D3D12_MESSAGE_SEVERITY_INFO;
    default:
        return D3D12_MESSAGE_SEVERITY_MESSAGE;
    }
}

/// Un message gardé par une file de debug, qui le rend en deux appels de `get(message, &size)` :
/// sa taille, puis le message, suivi de son texte. new[] aligne pour tout type fondamental, les
/// pointeurs du message compris. Vide si la file refuse.
template <typename Message, typename Get> std::unique_ptr<std::byte[]> readStoredMessage(Get get)
{
    SIZE_T size = 0;
    if (FAILED(get(static_cast<Message*>(nullptr), &size)))
    {
        return nullptr;
    }
    auto storage = std::make_unique_for_overwrite<std::byte[]>(size);
    if (FAILED(get(reinterpret_cast<Message*>(storage.get()), &size)))
    {
        return nullptr;
    }
    return storage;
}

std::unexpected<core::Error> failedCall(std::string_view call, HRESULT result)
{
    return core::makeError(
        core::ErrorCode::Unsupported,
        std::format("{} : HRESULT 0x{:08X}", call, static_cast<std::uint32_t>(result)));
}

/// La couche de debug, à activer avant de créer le device. Exigée, comme les couches de Vulkan :
/// sans elle, l'erreur au lieu d'un Debug qui ne vérifierait rien (règle n°7).
core::Result<void> enableDebugLayer()
{
    nvrhi::RefCountPtr<ID3D12Debug> debug;
    if (const HRESULT result = D3D12GetDebugInterface(iidOf(debug), outPointer(debug));
        FAILED(result))
    {
        return core::makeError(
            core::ErrorCode::Unsupported,
            std::format("couche de debug Direct3D 12 absente, exigée en Debug (HRESULT 0x{:08X}) : "
                        "d3d12SDKLayers.dll vient de la fonctionnalité facultative de Windows "
                        "« Outils graphiques » (Paramètres > Système > Fonctionnalités "
                        "facultatives)",
                        static_cast<std::uint32_t>(result)));
    }
    debug->EnableDebugLayer();
    return {};
}

/// Les messages que la couche de debug a gardés avant que le moteur ne s'inscrive (ceux de la
/// création du device), passés par le même chemin que les suivants, puis effacés : une erreur
/// émise avant l'inscription ne serait sinon jamais vue (règle n°7). Un message illisible compte
/// pour une erreur : on ne sait pas ce qu'il disait.
void drainStoredMessages(ID3D12InfoQueue1& queue)
{
    const UINT64 count = queue.GetNumStoredMessagesAllowedByRetrievalFilter();
    for (UINT64 index = 0; index < count; ++index)
    {
        const auto storage =
            readStoredMessage<D3D12_MESSAGE>([&](D3D12_MESSAGE* message, SIZE_T* size)
                                             { return queue.GetMessage(index, message, size); });
        if (!storage)
        {
            reportDebugMessage("d3d12", D3D12_MESSAGE_SEVERITY_ERROR,
                               "message gardé illisible (ID3D12InfoQueue::GetMessage)");
            continue;
        }
        const auto& message = *reinterpret_cast<const D3D12_MESSAGE*>(storage.get());
        onD3d12Message(message.Category, message.Severity, message.ID, message.pDescription,
                       nullptr);
    }
    queue.ClearStoredMessages();
}

/// Les messages de la couche de debug vers nos logs, et une assertion sur toute erreur. Le rappel
/// (`ID3D12InfoQueue1`) arrive avec Windows 11 : sans lui, les messages n'iraient qu'au débogueur
/// (OutputDebugString), et une erreur passerait sans bruit. On refuse donc (règle n°7).
core::Result<void> routeDebugMessages(D3d12Context& d3d12)
{
    if (const HRESULT result =
            d3d12.device->QueryInterface(iidOf(d3d12.infoQueue), outPointer(d3d12.infoQueue));
        FAILED(result))
    {
        return core::makeError(
            core::ErrorCode::Unsupported,
            std::format("la couche de debug Direct3D 12 ne sait pas rappeler le moteur "
                        "(ID3D12InfoQueue1, Windows 11 ; HRESULT 0x{:08X}) : ses erreurs ne "
                        "seraient pas vues. --gpu vulkan, ou un build Release",
                        static_cast<std::uint32_t>(result)));
    }
    if (const HRESULT result = d3d12.infoQueue->RegisterMessageCallback(
            onD3d12Message, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &d3d12.messageCookie);
        FAILED(result))
    {
        d3d12.infoQueue = nullptr; // rien à désinscrire
        return failedCall("ID3D12InfoQueue1::RegisterMessageCallback", result);
    }
    drainStoredMessages(*d3d12.infoQueue);
    return {};
}

/// La file de la couche de debug DXGI, dont la factory créée avec DXGI_CREATE_FACTORY_DEBUG remplit
/// les messages : un mauvais usage de la swapchain (un redimensionnement refusé, une présentation
/// fautive) n'est pas une erreur de Direct3D 12, et sa couche ne le voit pas. Exigée comme elle
/// (règle n°7) : sans elle, ces erreurs n'iraient qu'au débogueur.
core::Result<void> openDxgiDebugQueue(D3d12Context& d3d12)
{
    if (const HRESULT result =
            DXGIGetDebugInterface1(0, iidOf(d3d12.dxgiInfoQueue), outPointer(d3d12.dxgiInfoQueue));
        FAILED(result))
    {
        return core::makeError(
            core::ErrorCode::Unsupported,
            std::format("couche de debug DXGI absente, exigée en Debug (HRESULT 0x{:08X}) : "
                        "dxgidebug.dll vient de la fonctionnalité facultative de Windows "
                        "« Outils graphiques », comme la couche de Direct3D 12",
                        static_cast<std::uint32_t>(result)));
    }
    return {};
}

/// Le nom de l'adaptateur, que DXGI donne en UTF-16, en UTF-8 pour nos logs.
std::string utf8Of(const wchar_t* text)
{
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1)
    {
        return {};
    }
    std::string utf8(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8.data(), size, nullptr, nullptr);
    return utf8;
}

/// Le plus performant des adaptateurs matériels qui créent un device Direct3D 12 : le GPU discret
/// plutôt que l'iGPU, comme vk-bootstrap pour Vulkan. DXGI les range ainsi lui-même
/// (`EnumAdapterByGpuPreference`). Le rendu logiciel (« Microsoft Basic Render Driver », WARP) est
/// écarté : il ne se choisit qu'exprès (`warpAdapter`, #19).
core::Result<nvrhi::RefCountPtr<IDXGIAdapter1>> highPerformanceAdapter(IDXGIFactory6& factory)
{
    std::string rejected;
    for (UINT index = 0;; ++index)
    {
        nvrhi::RefCountPtr<IDXGIAdapter1> adapter;
        const HRESULT result = factory.EnumAdapterByGpuPreference(
            index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, iidOf(adapter), outPointer(adapter));
        // DXGI_ERROR_NOT_FOUND : la liste est finie. Tout autre échec est une panne, pas la fin
        // de la liste : on le dit, plutôt que de lire un adaptateur vide.
        if (result == DXGI_ERROR_NOT_FOUND)
        {
            break;
        }
        if (FAILED(result))
        {
            return failedCall(std::format("IDXGIFactory6::EnumAdapterByGpuPreference({})", index),
                              result);
        }
        // Un GetDesc1 raté laisserait `Flags` à zéro : WARP passerait le filtre pour un GPU.
        DXGI_ADAPTER_DESC1 desc{};
        if (const HRESULT descResult = adapter->GetDesc1(&desc); FAILED(descResult))
        {
            return failedCall("IDXGIAdapter1::GetDesc1", descResult);
        }
        const std::string name = utf8Of(desc.Description);
        if ((desc.Flags & static_cast<UINT>(DXGI_ADAPTER_FLAG_SOFTWARE)) != 0)
        {
            rejected += std::format("\n  - {} : rendu logiciel", name);
            continue;
        }
        // Sans device en sortie, D3D12CreateDevice ne fait que vérifier qu'il pourrait le créer.
        if (FAILED(D3D12CreateDevice(adapter, MinimumFeatureLevel, IID_ID3D12Device, nullptr)))
        {
            rejected += std::format("\n  - {} : pas de niveau 12_0", name);
            continue;
        }
        return adapter;
    }
    return core::makeError(core::ErrorCode::Unsupported,
                           std::format("aucun GPU Direct3D 12 compatible{}", rejected));
}

/// WARP, le rendu logiciel de Direct3D 12 (« Microsoft Basic Render Driver »), que DXGI fournit
/// lui-même (`IDXGIFactory4::EnumWarpAdapter`) : un runner de CI n'a pas de GPU, WARP est son seul
/// adaptateur. Le drapeau SOFTWARE est vérifié sur ce que DXGI rend : un adaptateur matériel ne
/// doit jamais passer pour WARP, ou les tests « WARP » resteraient verts sur un GPU, sans rien dire
/// du runner.
core::Result<nvrhi::RefCountPtr<IDXGIAdapter1>> warpAdapter(IDXGIFactory6& factory)
{
    nvrhi::RefCountPtr<IDXGIAdapter1> adapter;
    if (const HRESULT result = factory.EnumWarpAdapter(iidOf(adapter), outPointer(adapter));
        FAILED(result))
    {
        return failedCall("IDXGIFactory4::EnumWarpAdapter", result);
    }
    // Un GetDesc1 raté laisserait `desc` à zéro : le refus plus bas accuserait un adaptateur sans
    // nom, au lieu de la vraie cause.
    DXGI_ADAPTER_DESC1 desc{};
    if (const HRESULT result = adapter->GetDesc1(&desc); FAILED(result))
    {
        return failedCall("IDXGIAdapter1::GetDesc1", result);
    }
    if ((desc.Flags & static_cast<UINT>(DXGI_ADAPTER_FLAG_SOFTWARE)) == 0)
    {
        return core::makeError(core::ErrorCode::Unsupported,
                               std::format("EnumWarpAdapter a rendu « {} », qui n'est pas un "
                                           "adaptateur logiciel : refusé, il ne doit pas passer "
                                           "pour WARP",
                                           utf8Of(desc.Description)));
    }
    return adapter;
}

/// L'adaptateur que demandent les options : WARP sur demande, sinon le GPU le plus performant.
core::Result<nvrhi::RefCountPtr<IDXGIAdapter1>> chooseAdapter(IDXGIFactory6& factory,
                                                              Adapter adapter)
{
    return adapter == Adapter::Software ? warpAdapter(factory) : highPerformanceAdapter(factory);
}

/// Vrai sous le pilote vidéo « offscreen » de SDL, qui n'a pas de fenêtre Win32 (pas de HWND) : ni
/// swapchain ni bureau, le device dessine hors écran. Le pilote que SDL a retenu, pas la variable
/// d'environnement : SDL peut en avoir pris un autre.
bool isOffscreenVideoDriver()
{
    const char* driver = SDL_GetCurrentVideoDriver();
    return driver != nullptr && std::string_view{driver} == "offscreen";
}

/// Le plus haut shader model du device. CheckFeatureSupport refuse un modèle que le runtime ne
/// connaît pas encore : on part du plus récent des en-têtes et on descend.
D3D_SHADER_MODEL highestShaderModel(ID3D12Device& device)
{
    for (int model = D3D_HIGHEST_SHADER_MODEL; model >= D3D_SHADER_MODEL_5_1; --model)
    {
        D3D12_FEATURE_DATA_SHADER_MODEL data{static_cast<D3D_SHADER_MODEL>(model)};
        if (SUCCEEDED(device.CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &data, sizeof(data))))
        {
            return data.HighestShaderModel;
        }
    }
    return D3D_SHADER_MODEL_5_1;
}

/// « NVIDIA GeForce RTX 4070 Laptop GPU — pilote 32.0.16.1742 — Direct3D 12, shader model 6.8 ».
/// La version du pilote est celle de son pilote en mode utilisateur, que DXGI rend en quatre
/// nombres de 16 bits.
std::string describeAdapter(IDXGIAdapter1& adapter, D3D_SHADER_MODEL shaderModel)
{
    DXGI_ADAPTER_DESC1 desc{};
    adapter.GetDesc1(&desc);
    LARGE_INTEGER driver{};
    adapter.CheckInterfaceSupport(IID_IDXGIDevice, &driver);
    const auto high = static_cast<std::uint32_t>(driver.HighPart);
    const auto low = static_cast<std::uint32_t>(driver.LowPart);
    // D3D_SHADER_MODEL_6_8 vaut 0x68 : la version majeure, puis la mineure, en chiffres
    // hexadécimaux.
    const auto model = static_cast<std::uint32_t>(shaderModel);
    return std::format("{} — pilote {}.{}.{}.{} — Direct3D 12, shader model {}.{}",
                       utf8Of(desc.Description), high >> 16U, high & 0xFFFFU, low >> 16U,
                       low & 0xFFFFU, model >> 4U, model & 0xFU);
}

} // namespace

void drainDxgiMessages(IDXGIInfoQueue& queue)
{
    // DXGI_DEBUG_DXGI : les messages de DXGI seul ; ceux de Direct3D 12 passent par son rappel.
    const UINT64 count = queue.GetNumStoredMessagesAllowedByRetrievalFilters(DXGI_DEBUG_DXGI);
    for (UINT64 index = 0; index < count; ++index)
    {
        const auto storage = readStoredMessage<DXGI_INFO_QUEUE_MESSAGE>(
            [&](DXGI_INFO_QUEUE_MESSAGE* message, SIZE_T* size)
            { return queue.GetMessage(DXGI_DEBUG_DXGI, index, message, size); });
        if (!storage)
        {
            reportDebugMessage("dxgi", D3D12_MESSAGE_SEVERITY_ERROR,
                               "message gardé illisible (IDXGIInfoQueue::GetMessage)");
            continue;
        }
        const auto& message = *reinterpret_cast<const DXGI_INFO_QUEUE_MESSAGE*>(storage.get());
        reportDebugMessage(
            "dxgi", d3d12SeverityOf(message.Severity),
            std::format("{} (DXGI_INFO_QUEUE_MESSAGE_ID {})", message.pDescription, message.ID));
    }
    queue.ClearStoredMessages(DXGI_DEBUG_DXGI);
}

core::Result<GpuDevice> createD3d12Device(const platform::Window& window,
                                          const DeviceOptions& options)
{
    const bool enableValidation = options.enableValidation;
    auto d3d12 = std::make_unique<D3d12Context>();
    if (enableValidation)
    {
        if (auto enabled = enableDebugLayer(); !enabled)
        {
            return std::unexpected{std::move(enabled.error())};
        }
        if (auto opened = openDxgiDebugQueue(*d3d12); !opened)
        {
            return std::unexpected{std::move(opened.error())};
        }
    }

    // DXGI_CREATE_FACTORY_DEBUG : la couche de debug DXGI, pour la factory et ce qu'elle crée
    // (la swapchain), qui écrit dans la file ouverte plus haut.
    const UINT factoryFlags = enableValidation ? DXGI_CREATE_FACTORY_DEBUG : 0;
    if (const HRESULT result =
            CreateDXGIFactory2(factoryFlags, iidOf(d3d12->factory), outPointer(d3d12->factory));
        FAILED(result))
    {
        return failedCall("CreateDXGIFactory2", result);
    }
    auto adapter = chooseAdapter(*d3d12->factory, options.adapter);
    if (!adapter)
    {
        return std::unexpected{std::move(adapter.error())};
    }
    d3d12->adapter = std::move(*adapter);

    if (const HRESULT result = D3D12CreateDevice(d3d12->adapter, MinimumFeatureLevel,
                                                 iidOf(d3d12->device), outPointer(d3d12->device));
        FAILED(result))
    {
        return failedCall("D3D12CreateDevice", result);
    }
    if (enableValidation)
    {
        if (auto routed = routeDebugMessages(*d3d12); !routed)
        {
            return std::unexpected{std::move(routed.error())};
        }
    }

    const D3D_SHADER_MODEL shaderModel = highestShaderModel(*d3d12->device);
    core::log("gpu", core::LogLevel::Info, "{}", describeAdapter(*d3d12->adapter, shaderModel));
    if (shaderModel < MinimumShaderModel)
    {
        return core::makeError(core::ErrorCode::Unsupported,
                               "le GPU n'a pas le shader model 6.0 des shaders DXIL");
    }

    // Une seule queue, comme sous Vulkan : elle dessine, et la swapchain présente dessus.
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (const HRESULT result = d3d12->device->CreateCommandQueue(&queueDesc, iidOf(d3d12->queue),
                                                                 outPointer(d3d12->queue));
        FAILED(result))
    {
        return failedCall("CreateCommandQueue", result);
    }
    d3d12->queue->SetName(L"graphics");

    nvrhi::d3d12::DeviceDesc desc;
    desc.errorCB = &nvrhiMessages();
    desc.pDevice = d3d12->device;
    desc.pGraphicsCommandQueue = d3d12->queue;
    const nvrhi::DeviceHandle d3d12Device = nvrhi::d3d12::createDevice(desc);
    if (!d3d12Device)
    {
        return core::makeError(core::ErrorCode::Unsupported, "nvrhi::d3d12::createDevice a échoué");
    }

    // La couche de validation de NVRHI vérifie l'usage de NVRHI ; la couche de debug, ce que NVRHI
    // envoie à Direct3D 12 (comme sous Vulkan).
    nvrhi::DeviceHandle nvrhiDevice = d3d12Device;
    if (enableValidation)
    {
        nvrhiDevice = nvrhi::validation::createValidationLayer(d3d12Device);
    }

    // SDL_video.h, SDL_PROP_WINDOW_WIN32_HWND_POINTER : la fenêtre Win32 où DXGI présente.
    auto* hwnd = static_cast<HWND>(SDL_GetPointerProperty(
        SDL_GetWindowProperties(window.handle.get()), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (hwnd == nullptr)
    {
        // Sans HWND, seul le pilote offscreen est connu et voulu (tests, CI sans bureau,
        // --capture). Pas de swapchain alors, l'image se dessine dans `offscreen` (beginFrame).
        // Tout autre pilote sans HWND est une panne, que le moteur ne doit pas prendre pour un
        // rendu hors écran.
        if (!isOffscreenVideoDriver())
        {
            return core::makeError(core::ErrorCode::Unsupported, "la fenêtre SDL n'a pas de HWND");
        }
        // La couche de debug DXGI ne rappelle pas le moteur, et la swapchain, qui relit sa file
        // (createD3d12Swapchain), n'existe pas ici : sans cette relecture, ce que DXGI a dit
        // depuis la création de la factory serait perdu, sur le seul chemin que la CI lance. Sans
        // swapchain, DXGI ne fait plus rien après le device : une seule relecture couvre tout
        // (règle n°7).
        if (d3d12->dxgiInfoQueue)
        {
            drainDxgiMessages(*d3d12->dxgiInfoQueue);
        }
        core::log("gpu", core::LogLevel::Info,
                  "Direct3D 12 sous le pilote offscreen de SDL : rendu hors écran (--capture)");
        return GpuDevice{.native =
                             std::unique_ptr<NativeDevice, NativeDeviceDeleter>{d3d12.release()},
                         .nvrhi = std::move(nvrhiDevice),
                         .swapchain = nullptr,
                         .offscreen = nullptr};
    }
    auto swapchain =
        createD3d12Swapchain(*d3d12, d3d12Device, hwnd, platform::windowPixelSize(window));
    if (!swapchain)
    {
        return std::unexpected{std::move(swapchain.error())};
    }

    return GpuDevice{.native = std::unique_ptr<NativeDevice, NativeDeviceDeleter>{d3d12.release()},
                     .nvrhi = std::move(nvrhiDevice),
                     .swapchain = std::move(*swapchain),
                     .offscreen = nullptr};
}

} // namespace levain::gpu
