#pragma once

// Les textures des trois couches du terrain (M5.6), de Poly Haven (CC0, tools/assets.lock) :
// l'herbe, un sol rocailleux et la roche, chacune en couleur, normale et rugosité. Chaque sorte est
// un tableau de trois textures, une couche par élément : trois bindings au lieu de neuf, quand
// WebGPU n'en admet que seize par étage de shader.

#include <array>
#include <filesystem>
#include <string_view>

#include <glm/glm.hpp>
#include <glm/gtc/type_precision.hpp>
#include <nvrhi/nvrhi.h>

#include "levain/core/error.hpp"

namespace levain::terrain
{

/// Les textures des couches, dans l'ordre des canaux de la carte de poids.
// rocky_terrain_02 est une herbe semée de pierres, et grass_path_2 un gravier parsemé de touffes :
// les noms de Poly Haven trompent, seul l'aperçu le dit.
inline constexpr std::array<std::string_view, 3> LayerTextureNames{"rocky_terrain_02",
                                                                   "grass_path_2", "rock_face"};

struct LayerTextures
{
    nvrhi::TextureHandle albedo;    ///< sRGB.
    nvrhi::TextureHandle normal;    ///< Dans l'espace tangent, le vert vers le haut (OpenGL).
    nvrhi::TextureHandle roughness; ///< Dans le rouge.
};

/// Charge les couches depuis `directory`, mips comprises, et enregistre leur envoi dans
/// `commandList`. Un fichier manquant n'est pas une erreur : chaque couche prend alors une couleur
/// unie (le navigateur, qui n'embarque pas ces textures), et la ligne du journal le dit.
[[nodiscard]] core::Result<LayerTextures> loadLayerTextures(nvrhi::IDevice& device,
                                                            nvrhi::ICommandList& commandList,
                                                            const std::filesystem::path& directory);

} // namespace levain::terrain
