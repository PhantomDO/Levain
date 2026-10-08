/*
 * Adapté de Donut, src/app/dx12/DeviceManager_DX12.cpp (https://github.com/NVIDIA-RTX/Donut),
 * CreateSwapChain, CreateRenderTargets, ResizeSwapChain et Present :
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

// La swapchain DXGI de Direct3D 12 : le pendant de swapchain_vk.cpp (ADR-0035, M1.4). Windows
// seulement.

#include <cstdint>
#include <deque>
#include <format>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include "d3d12_context.hpp"
#include "native_device.hpp"

#include "levain/core/error.hpp"
#include "levain/core/log.hpp"
#include "levain/gpu/device.hpp"

namespace levain::gpu
{

namespace
{

/// Trois images, comme Donut : DXGI en exige deux en *flip model*, une de plus laisse le GPU
/// dessiner pendant que l'écran en garde une et qu'une autre attend son tour.
constexpr UINT BufferCount = 3;

/// DXGI refuse une swapchain sRGB en *flip model* : elle est en BGRA8 linéaire, et NVRHI y dessine
/// par une vue sRGB, que Direct3D 12 permet sur ces images-là. Le moteur voit le même format que
/// sous Vulkan, B8G8R8A8_SRGB, et ses pipelines n'en savent rien (comme Donut).
constexpr DXGI_FORMAT StorageFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
constexpr nvrhi::Format ViewFormat = nvrhi::Format::SBGRA8_UNORM;

struct D3d12Swapchain;

nvrhi::ITexture* acquireImage(D3d12Swapchain& swapchain, platform::PixelSize size);
void presentImage(D3d12Swapchain& swapchain);

/// La Swapchain de Direct3D 12. Ses fonctions virtuelles ne font qu'appeler les fonctions libres de
/// ce fichier.
struct D3d12Swapchain final : Swapchain
{
    D3d12Swapchain() = default;
    D3d12Swapchain(const D3d12Swapchain&) = delete;
    D3d12Swapchain& operator=(const D3d12Swapchain&) = delete;
    D3d12Swapchain(D3d12Swapchain&&) = delete;
    D3d12Swapchain& operator=(D3d12Swapchain&&) = delete;
    ~D3d12Swapchain() override;

    nvrhi::Format format() const override { return ViewFormat; }

    nvrhi::ITexture* acquire(platform::PixelSize size) override
    {
        return acquireImage(*this, size);
    }

    void present() override { presentImage(*this); }

    /// Le device NVRHI sans sa couche de validation, gardé : les images en sont des textures.
    nvrhi::DeviceHandle nvrhi;
    nvrhi::RefCountPtr<ID3D12CommandQueue> queue;
    /// Signalée sur la queue après chaque présentation : de quoi attendre que la dernière ait fini.
    nvrhi::RefCountPtr<ID3D12Fence> fence;
    std::uint64_t fenceValue = 0;
    /// Avec la validation seulement : la file de la couche de debug DXGI, relue après chaque
    /// présentation et chaque redimensionnement. Pas à la destruction : un destructeur ne
    /// journalise pas (un log peut lever, `signalQueue`), et sans plein écran exclusif (Alt+Entrée
    /// coupé, le plein écran laissé à SDL), DXGI n'a rien à y dire. Mesuré sur la 4070 : aucun
    /// message gardé avant ni après la dernière libération de `swapchain`, à deux fermetures du
    /// sandbox.
    nvrhi::RefCountPtr<IDXGIInfoQueue> dxgiInfoQueue;
    nvrhi::RefCountPtr<IDXGISwapChain3> swapchain;
    /// Vide, et `size` nulle, après une reconstruction ratée : la frame suivante la retente.
    std::vector<nvrhi::TextureHandle> images;
    platform::PixelSize size{};

    std::deque<nvrhi::EventQueryHandle> framesInFlight;
};

/// Ce que la couche de debug DXGI a dit depuis la dernière fois ; rien sans la validation, qui
/// seule ouvre sa file (`queue` nulle).
void checkDxgiMessages(IDXGIInfoQueue* queue)
{
    if (queue != nullptr)
    {
        drainDxgiMessages(*queue);
    }
}

/// Signale la *fence* après tout ce que la queue a déjà reçu, présentation comprise. Rend l'échec
/// de la queue (device perdu) sans le journaliser, à l'appelant de le dire : le destructeur, qui
/// s'en sert, ne doit rien lever, et un log peut lever (`std::format`).
HRESULT signalQueue(D3d12Swapchain& swapchain)
{
    ++swapchain.fenceValue;
    return swapchain.queue->Signal(swapchain.fence, swapchain.fenceValue);
}

/// Attend que la queue ait tout fini, présentations comprises. `nvrhi::IDevice::waitForIdle` ne
/// suffit pas sous Direct3D 12 : il n'attend que la *fence* de NVRHI, signalée après sa dernière
/// command list, pas la présentation qui la suit sur la queue (NVRHI, d3d12-device.cpp) ; une image
/// relâchée alors est une corruption pour la couche de debug (D3D12_MESSAGE_ID 921, vu à la
/// fermeture). Vulkan n'a pas le piège : NVRHI y appelle vkDeviceWaitIdle (vulkan-device.cpp).
/// Un signal refusé n'est pas attendu : sa valeur ne serait jamais atteinte.
HRESULT waitForQueue(D3d12Swapchain& swapchain)
{
    const HRESULT result = signalQueue(swapchain);
    if (SUCCEEDED(result))
    {
        // Sans événement, SetEventOnCompletion ne rend la main qu'une fois la valeur atteinte.
        swapchain.fence->SetEventOnCompletion(swapchain.fenceValue, nullptr);
    }
    return result;
}

D3d12Swapchain::~D3d12Swapchain()
{
    // La dernière frame et sa présentation peuvent encore être en cours sur le GPU. Sans fence, la
    // création a échoué avant la première présentation : rien n'est en cours. Un signal refusé ici
    // (device perdu) n'a plus rien à attendre, et la présentation l'a déjà dit.
    if (fence)
    {
        std::ignore = waitForQueue(*this);
    }
    images.clear();
    framesInFlight.clear();
    if (nvrhi)
    {
        nvrhi->runGarbageCollection();
    }
}

/// Une swapchain sans images, que `acquireImage` reconstruira à la frame suivante.
void forgetImages(D3d12Swapchain& swapchain)
{
    swapchain.images.clear();
    swapchain.size = {};
}

/// Enveloppe les images de la swapchain en textures NVRHI.
core::Result<void> wrapImages(D3d12Swapchain& swapchain)
{
    for (UINT index = 0; index < BufferCount; ++index)
    {
        nvrhi::RefCountPtr<ID3D12Resource> image;
        if (const HRESULT result =
                swapchain.swapchain->GetBuffer(index, iidOf(image), outPointer(image));
            FAILED(result))
        {
            return core::makeError(core::ErrorCode::Unsupported,
                                   std::format("IDXGISwapChain::GetBuffer : HRESULT 0x{:08X}",
                                               static_cast<std::uint32_t>(result)));
        }
        nvrhi::TextureDesc desc;
        desc.width = static_cast<std::uint32_t>(swapchain.size.width);
        desc.height = static_cast<std::uint32_t>(swapchain.size.height);
        desc.format = ViewFormat;
        desc.debugName = "swapchain";
        desc.isRenderTarget = true;
        // État « Present » conservé, comme sous Vulkan : NVRHI y remet l'image à la fermeture de
        // chaque command list qui l'a touchée.
        desc.initialState = nvrhi::ResourceStates::Present;
        desc.keepInitialState = true;
        swapchain.images.push_back(swapchain.nvrhi->createHandleForNativeTexture(
            nvrhi::ObjectTypes::D3D12_Resource, nvrhi::Object(image.Get()), desc));
    }
    return {};
}

/// Redimensionne les images. DXGI refuse tant qu'une référence à l'une d'elles survit : celles des
/// command lists que NVRHI garde jusqu'à son ramasse-miettes comprises (Donut,
/// ReleaseRenderTargets), d'où l'attente du GPU puis le ramasse-miettes avant ResizeBuffers. En cas
/// d'échec, plus aucune image (`forgetImages`), plutôt que des images à moitié refaites.
core::Result<void> resizeImages(D3d12Swapchain& swapchain, platform::PixelSize size)
{
    if (const HRESULT result = waitForQueue(swapchain); FAILED(result))
    {
        return core::makeError(core::ErrorCode::Unsupported,
                               std::format("ID3D12CommandQueue::Signal : HRESULT 0x{:08X}",
                                           static_cast<std::uint32_t>(result)));
    }
    forgetImages(swapchain);
    swapchain.framesInFlight.clear();
    swapchain.nvrhi->runGarbageCollection();

    // 0 et DXGI_FORMAT_UNKNOWN : le nombre d'images et le format ne changent pas.
    const HRESULT result = swapchain.swapchain->ResizeBuffers(
        0, static_cast<UINT>(size.width), static_cast<UINT>(size.height), DXGI_FORMAT_UNKNOWN, 0);
    // Ce que DXGI dit d'un refus passe avant notre propre message, qui ne donne que le code.
    checkDxgiMessages(swapchain.dxgiInfoQueue);
    if (FAILED(result))
    {
        return core::makeError(core::ErrorCode::Unsupported,
                               std::format("IDXGISwapChain::ResizeBuffers : HRESULT 0x{:08X}",
                                           static_cast<std::uint32_t>(result)));
    }
    swapchain.size = size;
    if (auto wrapped = wrapImages(swapchain); !wrapped)
    {
        forgetImages(swapchain);
        return wrapped;
    }
    return {};
}

bool matchesSize(platform::PixelSize left, platform::PixelSize right)
{
    return left.width == right.width && left.height == right.height;
}

nvrhi::ITexture* acquireImage(D3d12Swapchain& swapchain, platform::PixelSize size)
{
    // DXGI ne redimensionne jamais seul : c'est à nous de comparer à la taille de la fenêtre,
    // comme sous Wayland. Une reconstruction ratée ne laisse aucune image : la frame suivante la
    // retente, même si la fenêtre a gardé l'ancienne taille.
    if (swapchain.images.size() != BufferCount || !matchesSize(swapchain.size, size))
    {
        if (auto resized = resizeImages(swapchain, size); !resized)
        {
            core::log("gpu", core::LogLevel::Error, "{}", resized.error().message);
            return nullptr;
        }
    }
    // Pas d'acquisition ni de sémaphore, à la différence de Vulkan : DXGI désigne l'image suivante,
    // et fait lui-même attendre la queue tant que l'écran la montre encore.
    return swapchain.images[swapchain.swapchain->GetCurrentBackBufferIndex()];
}

void presentImage(D3d12Swapchain& swapchain)
{
    // Intervalle 1 : la présentation attend le rafraîchissement de l'écran, comme FIFO sous Vulkan.
    // Elle passe par la queue de NVRHI, après les command lists de la frame.
    if (const HRESULT result = swapchain.swapchain->Present(1, 0); FAILED(result))
    {
        core::log("gpu", core::LogLevel::Error, "IDXGISwapChain::Present : HRESULT 0x{:08X}",
                  static_cast<std::uint32_t>(result));
    }
    checkDxgiMessages(swapchain.dxgiInfoQueue);
    if (const HRESULT result = signalQueue(swapchain); FAILED(result))
    {
        core::log("gpu", core::LogLevel::Error, "ID3D12CommandQueue::Signal : HRESULT 0x{:08X}",
                  static_cast<std::uint32_t>(result));
    }
    limitFramesInFlight(swapchain.framesInFlight, *swapchain.nvrhi);
    swapchain.nvrhi->runGarbageCollection();
}

} // namespace

core::Result<std::unique_ptr<Swapchain, SwapchainDeleter>>
createD3d12Swapchain(const D3d12Context& d3d12, nvrhi::DeviceHandle nvrhi, HWND window,
                     platform::PixelSize size)
{
    // FLIP_DISCARD : le *flip model*, le seul que Direct3D 12 accepte ; l'image présentée est
    // rendue à DXGI, son contenu perdu, comme une swapchain Vulkan.
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = static_cast<UINT>(size.width);
    desc.Height = static_cast<UINT>(size.height);
    desc.Format = StorageFormat;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = BufferCount;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    // ReleaseAndGetAddressOf, pas `&` : l'adresse typée que l'appel veut, vidée d'abord
    // (d3d12_context.hpp, outPointer).
    nvrhi::RefCountPtr<IDXGISwapChain1> swapchain1;
    if (const HRESULT result = d3d12.factory->CreateSwapChainForHwnd(
            d3d12.queue, window, &desc, nullptr, nullptr, swapchain1.ReleaseAndGetAddressOf());
        FAILED(result))
    {
        // Ce que DXGI dit d'un refus passe avant notre message, qui ne donne que le code (comme
        // resizeImages). De même avant chaque refus qui suit.
        checkDxgiMessages(d3d12.dxgiInfoQueue);
        return core::makeError(core::ErrorCode::Unsupported,
                               std::format("IDXGIFactory2::CreateSwapChainForHwnd : HRESULT "
                                           "0x{:08X}",
                                           static_cast<std::uint32_t>(result)));
    }
    // Alt+Entrée ne fait pas passer DXGI en plein écran exclusif : le plein écran reste à SDL
    // (Donut fait de même).
    d3d12.factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);

    auto swapchain = std::make_unique<D3d12Swapchain>();
    swapchain->nvrhi = std::move(nvrhi);
    swapchain->queue = d3d12.queue;
    swapchain->dxgiInfoQueue = d3d12.dxgiInfoQueue;
    swapchain->size = size;
    if (const HRESULT result = d3d12.device->CreateFence(
            0, D3D12_FENCE_FLAG_NONE, iidOf(swapchain->fence), outPointer(swapchain->fence));
        FAILED(result))
    {
        checkDxgiMessages(d3d12.dxgiInfoQueue);
        return core::makeError(core::ErrorCode::Unsupported,
                               std::format("ID3D12Device::CreateFence : HRESULT 0x{:08X}",
                                           static_cast<std::uint32_t>(result)));
    }
    if (const HRESULT result = swapchain1->QueryInterface(iidOf(swapchain->swapchain),
                                                          outPointer(swapchain->swapchain));
        FAILED(result))
    {
        checkDxgiMessages(d3d12.dxgiInfoQueue);
        return core::makeError(core::ErrorCode::Unsupported,
                               "IDXGISwapChain3 absente (GetCurrentBackBufferIndex)");
    }
    if (auto wrapped = wrapImages(*swapchain); !wrapped)
    {
        checkDxgiMessages(d3d12.dxgiInfoQueue);
        return std::unexpected{std::move(wrapped.error())};
    }
    // Ce que DXGI a dit depuis la création de la factory, avant la première présentation.
    checkDxgiMessages(d3d12.dxgiInfoQueue);
    return std::unique_ptr<Swapchain, SwapchainDeleter>{swapchain.release()};
}

} // namespace levain::gpu
