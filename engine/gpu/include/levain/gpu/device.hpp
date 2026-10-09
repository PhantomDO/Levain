#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string_view>

#include <nvrhi/nvrhi.h>

#include "levain/core/error.hpp"
#include "levain/gpu/webgpu.hpp"
#include "levain/platform/window.hpp"

namespace levain::gpu
{

#ifndef __EMSCRIPTEN__
/// Ce que NVRHI ne crée pas lui-même, propre à chaque backend : instance, surface et device Vulkan
/// (`vulkan_context.hpp`), ou factory DXGI, device et queue Direct3D 12 (`d3d12_context.hpp`). Une
/// classe de base, une dérivée par backend : les deux backends natifs vivent dans le même exe
/// Windows, où un même nom ne peut avoir qu'une définition. Ni Vulkan ni Direct3D n'apparaissent
/// dans cet en-tête.
struct NativeDevice;

/// Détruit les objets du backend, dans l'ordre inverse de leur création.
struct NativeDeviceDeleter
{
    void operator()(NativeDevice* device) const noexcept;
};

/// La swapchain, ses images enveloppées en textures NVRHI, et la cadence des frames : une
/// interface, une implémentation par backend (`swapchain_vk.cpp`).
struct Swapchain;

/// Attend que le GPU ait fini, puis détruit les images et la swapchain du backend.
struct SwapchainDeleter
{
    void operator()(Swapchain* swapchain) const noexcept;
};
#endif

/// Le backend quand rien ne le choisit (`--gpu` absent) : Vulkan en natif, WebGPU dans le
/// navigateur, qui n'a que lui (ADR-0023). Un défaut que la cible refuserait ferait échouer tout
/// lancement sans option : le test `backends_test.cpp` vérifie que `requireBackendBuilt` l'accepte,
/// sur chaque cible.
#ifdef __EMSCRIPTEN__
inline constexpr nvrhi::GraphicsAPI DefaultBackend = nvrhi::GraphicsAPI::WEBGPU;
#else
inline constexpr nvrhi::GraphicsAPI DefaultBackend = nvrhi::GraphicsAPI::VULKAN;
#endif

struct DeviceOptions
{
    /// Couches de validation Vulkan (ou couche de debug Direct3D 12) et couche de validation NVRHI,
    /// exigées en Debug (règle n°4). Une erreur de l'une ou de l'autre arrête le programme sur une
    /// assertion.
    bool enableValidation = false;
    /// Vulkan, Direct3D 12 (un build Windows seulement, ADR-0035), ou WebGPU sur Dawn (ADR-0023) :
    /// ce dernier sert à développer et vérifier le backend WebGPU sans navigateur. WebGPU, et
    /// Direct3D 12 tant qu'il n'a pas de swapchain, dessinent hors écran : la fenêtre reste vide,
    /// `--capture` montre l'image.
    nvrhi::GraphicsAPI api = DefaultBackend;
};

/// Le backend que nomme la ligne de commande (`--gpu`, les tests GPU) : « vulkan », « d3d12 » ou
/// « webgpu ». Vide pour un autre nom. « d3d12 » se lit partout : c'est `requireBackendBuilt` qui
/// le refuse hors de Windows, avec un message qui le dit.
[[nodiscard]] std::optional<nvrhi::GraphicsAPI> graphicsApiNamed(std::string_view name);

/// Refuse un backend que ce build ne contient pas : Direct3D 12 hors de Windows, tout sauf WebGPU
/// dans le navigateur. Un refus de l'option n'afficherait que l'usage du programme, sans dire
/// pourquoi « d3d12 » n'est pas pris.
[[nodiscard]] core::Result<void> requireBackendBuilt(nvrhi::GraphicsAPI api);

#ifndef __EMSCRIPTEN__
/// Le GPU vu par le moteur : un `nvrhi::IDevice`, les objets du backend qui le portent, et la
/// swapchain où il dessine.
///
/// **L'ordre des membres est l'ordre de destruction inverse** : `swapchain`, puis `nvrhi`, puis
/// `native`. Les images de la swapchain sont des textures NVRHI, qui doivent disparaître avant le
/// device NVRHI ; lui-même doit disparaître avant le device natif sur lequel il libère ses
/// ressources. Pour la même raison, ne gardez pas de `nvrhi::DeviceHandle` plus longtemps que le
/// `GpuDevice`.
struct GpuDevice
{
    std::unique_ptr<NativeDevice, NativeDeviceDeleter> native;
    nvrhi::DeviceHandle nvrhi;
    std::unique_ptr<Swapchain, SwapchainDeleter> swapchain;
    /// Sans swapchain (WebGPU en natif, Direct3D 12 pour l'instant), l'image où dessiner, à la
    /// taille de la fenêtre.
    nvrhi::TextureHandle offscreen;
};
#else
/// Dans le navigateur : le device NVRHI sur WebGPU et le canvas de la fenêtre, où il dessine
/// (ADR-0023). Le canvas disparaît avant le device.
struct GpuDevice
{
    nvrhi::DeviceHandle nvrhi;
    WebGpuCanvasHandle canvas;
};
#endif

using GpuDeviceCallback = std::function<void(core::Result<GpuDevice>)>;

/// Le device, par callback : dans le navigateur, il arrive une fois la main rendue (ADR-0023,
/// point 2), sur WebGPU ; en natif, avant le retour, comme `createGpuDevice`. Un backend que la
/// cible n'a pas (`requireBackendBuilt`) n'arrive pas : le callback reçoit le refus avant le
/// retour, et le navigateur ne lance pas WebGPU à la place sans rien dire.
void requestGpuDevice(const platform::Window& window, const DeviceOptions& options,
                      const GpuDeviceCallback& onDevice);

#ifndef __EMSCRIPTEN__
/// Crée le device du backend choisi sur le GPU le plus adapté (discret de préférence), puis le
/// device NVRHI par-dessus. Le nom du GPU et la version du pilote sont journalisés dans la
/// catégorie `gpu`.
///
/// Échoue pour un backend absent de ce build (`requireBackendBuilt`), ou sans GPU compatible :
/// - Vulkan : 1.3, dynamicRendering, synchronization2, timeline semaphores ;
/// - Direct3D 12 : un adaptateur matériel au niveau 12_0 et au shader model 6.0 (WARP écarté).
///
/// Avec la validation, échoue aussi sans ses couches : sous Vulkan, les couches de validation ;
/// sous Direct3D 12, la couche de debug (`d3d12SDKLayers.dll`, la fonctionnalité facultative
/// « Outils graphiques » de Windows) et `ID3D12InfoQueue1` (Windows 11, Windows Server 2025), par
/// laquelle elle rappelle le moteur.
[[nodiscard]] core::Result<GpuDevice> createGpuDevice(const platform::Window& window,
                                                      const DeviceOptions& options);
#endif

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
