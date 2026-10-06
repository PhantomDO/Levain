#pragma once

// Le hot-reload des textures (ADR-0021) : les fichiers des modèles en usage, relus quand ils
// changent sur le disque, sans relancer le programme.

#include <chrono>
#include <map>

#include <nvrhi/nvrhi.h>

#include "levain/app/models.hpp"
#include "levain/assets/asset_ref.hpp"
#include "levain/assets/registry.hpp"
#include "levain/render/mesh_pass.hpp"

namespace levain::app
{

/// Le hot-reload des textures (ADR-0021) : les fichiers du registre, relus une fois par période.
struct TextureReload
{
    assets::AssetWatch watch;
    std::chrono::steady_clock::time_point nextCheck;
};

[[nodiscard]] TextureReload startTextureReload(const assets::AssetRegistry& registry);

/// Recharge les textures modifiées sur le disque, depuis leur source puisque leur fichier cuit est
/// périmé, et refait les binding sets des modèles qui les utilisent. Une texture qui ne se charge
/// pas (fichier invalide, ou à moitié écrit) reste en place, et l'erreur va dans le log.
void reloadChangedTextures(TextureReload& reload, nvrhi::IDevice& device,
                           assets::AssetRegistry& registry, const assets::ModelCache& modelCache,
                           std::map<assets::AssetId, ModelGpu>& models,
                           const render::MeshPass& meshPass, nvrhi::ISampler& sampler);

} // namespace levain::app
