#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

#include <nvrhi/nvrhi.h>

#include "levain/core/error.hpp"

namespace levain::gpu
{

struct WebGpuOptions
{
    /// La couche de validation de NVRHI par-dessus le backend, exigée en Debug (règle n°4).
    bool enableValidation = false;
    /// Où vont les messages de NVRHI et les erreurs de WebGPU. Par défaut, nos logs et une
    /// assertion sur toute erreur. Doit survivre au device.
    nvrhi::IMessageCallback* messageCallback = nullptr;
    /// Pour le test du refus (tests/webgpu_test.cpp) : demande à Dawn son backend Null, que
    /// `createWebGpuDevice` doit refuser. En natif seulement.
    bool forceNullBackend = false;
};

using WebGpuDeviceCallback = std::function<void(core::Result<nvrhi::DeviceHandle>)>;

/// Un device NVRHI sur WebGPU (ADR-0023). `onDevice` reçoit le device, ou l'erreur : dans le
/// navigateur, plus tard, une fois la main rendue ; en natif, sur Dawn, avant le retour. Le nom de
/// l'adaptateur est journalisé dans la catégorie `gpu`.
void requestWebGpuDevice(const WebGpuOptions& options, const WebGpuDeviceCallback& onDevice);

#ifndef __EMSCRIPTEN__
/// En natif seulement, la même chose sans callback : pour dessiner hors écran (tests). Refuse le
/// backend Null de Dawn, qui accepte tout et ne dessine rien (create.cpp).
[[nodiscard]] core::Result<nvrhi::DeviceHandle> createWebGpuDevice(const WebGpuOptions& options);
#else
/// La surface d'un canvas HTML : l'image où dessiner à chaque frame. Le navigateur la présente
/// seul, quand la frame rend la main.
struct WebGpuCanvas;

struct WebGpuCanvasDeleter
{
    void operator()(WebGpuCanvas* canvas) const noexcept;
};

using WebGpuCanvasHandle = std::unique_ptr<WebGpuCanvas, WebGpuCanvasDeleter>;

/// La surface du canvas désigné par `selector` (« #canvas »), pour un device de
/// `requestWebGpuDevice`. `srgb` : le moteur y dessine en sRGB, comme dans la swapchain Vulkan ;
/// sans, les valeurs arrivent telles quelles, comme dans la cible du test de fumée.
[[nodiscard]] core::Result<WebGpuCanvasHandle>
createWebGpuCanvas(nvrhi::IDevice& device, std::string_view selector, bool srgb = true);

/// Le format de l'image du canvas, pour créer les pipelines qui y dessinent : du sRGB par défaut,
/// comme la swapchain Vulkan, pour que les couleurs soient les mêmes (ADR-0023, point 5).
[[nodiscard]] nvrhi::Format canvasFormat(const WebGpuCanvas& canvas);

/// L'image où dessiner cette frame, à la taille `width` × `height` en pixels : la surface est
/// reconfigurée quand elle change. `nullptr` si le navigateur n'en donne pas (onglet caché).
[[nodiscard]] nvrhi::ITexture* beginCanvasFrame(WebGpuCanvas& canvas, std::uint32_t width,
                                                std::uint32_t height);
#endif

} // namespace levain::gpu
