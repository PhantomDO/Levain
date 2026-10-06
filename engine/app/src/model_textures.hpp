#pragma once

// Les textures des modèles, partagées par l'envoi (models.cpp) et le hot-reload
// (texture_reload.cpp) : internes au module.

#include <cstddef>

#include <nvrhi/nvrhi.h>

#include "levain/app/models.hpp"
#include "levain/assets/asset_ref.hpp"
#include "levain/assets/cooked_texture.hpp"
#include "levain/assets/registry.hpp"
#include "levain/core/error.hpp"

namespace levain::app
{

/// Le format où charger les textures cuites : le BC7 si le GPU l'échantillonne (tous les GPU de
/// PC), le RGBA8 sinon.
[[nodiscard]] assets::TextureFormat textureTargetOf(nvrhi::IDevice& device);

struct UploadedTexture
{
    nvrhi::TextureHandle handle;
    std::size_t bytes = 0; ///< En mémoire vidéo, mips comprises.
};

/// Charge la texture `key`, cuite si possible (ADR-0020) : le cache BC7 se copie tel quel, sinon
/// la source. Son envoi est enregistré dans `commandList`.
[[nodiscard]] core::Result<UploadedTexture>
uploadTexture(nvrhi::IDevice& device, nvrhi::ICommandList& commandList,
              const assets::AssetRegistry& registry, const assets::ModelCache& models,
              TextureKey key, assets::TextureFormat target);

} // namespace levain::app
