#pragma once

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
};

/// Un device NVRHI sur WebGPU (ADR-0023), sans fenêtre : pour dessiner hors écran et comparer le
/// résultat à celui de Vulkan. Pour l'instant en natif seulement, sur Dawn (Vulkan en dessous) ;
/// dans le navigateur, le device arrive de façon asynchrone et demande sa propre entrée (#184,
/// partie C).
///
/// Échoue sans adaptateur WebGPU, ou si le device est refusé. Le nom de l'adaptateur est journalisé
/// dans la catégorie `gpu`.
[[nodiscard]] core::Result<nvrhi::DeviceHandle> createWebGpuDevice(const WebGpuOptions& options);

} // namespace levain::gpu
