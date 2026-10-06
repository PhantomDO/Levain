#pragma once

#include <cstdint>
#include <filesystem>

#include "levain/assets/collision.hpp"
#include "levain/assets/gltf.hpp"
#include "levain/core/error.hpp"

namespace levain::assets
{

/// La version du cuiseur : un fichier cuit par une autre version n'est plus « à jour » (ADR-0020),
/// et le moteur retombe sur la source. À incrémenter dès que ce qu'écrit le cuiseur change.
/// Le premier réglage d'import (dans le `.meta`) devra entrer dans l'en-tête avec elle : sinon,
/// changer ce réglage ne recuira rien (étude E4).
inline constexpr std::uint32_t CookerVersion = 1;

/// Comment les tableaux d'un `.lvmesh` sont encodés. Seul le brut existe ; les autres valeurs sont
/// réservées à une compression, si la mémoire ou l'espace d'une console ou d'un téléphone la
/// réclament (demande de Donnovan, ADR-0020), et refusées d'ici là.
// NOLINTNEXTLINE(performance-enum-size) : un champ de 4 octets du format, qui garde de la place.
enum class MeshEncoding : std::uint32_t
{
    Raw = 0,
};

/// Écrit `model` en `.lvmesh` : un en-tête (signature `LVMS`, version du format, encodage, version
/// du cuiseur, hash de la source), puis les meshes, les nœuds, les matériaux et les images
/// embarquées, tels que le moteur les attend. Petit-boutiste.
[[nodiscard]] core::Result<void> writeCookedModel(const std::filesystem::path& path,
                                                  const Model& model, std::uint64_t sourceHash);

/// Relit un `.lvmesh`. Échoue s'il est illisible, tronqué, d'un autre encodage, ou **périmé** :
/// cuit par une autre version du cuiseur, ou depuis une source dont le hash n'est plus
/// `sourceHash`. Le moteur retombe alors sur la source.
[[nodiscard]] core::Result<Model> readCookedModel(const std::filesystem::path& path,
                                                  std::uint64_t sourceHash);

/// Écrit la collision d'un modèle en `.lvcol` (ADR-0028), à côté de son `.lvmesh` : un en-tête
/// (signature `LVCO`, version du format, version du cuiseur, hash de la source, tolérance), puis
/// ses sommets et ses indices. Petit-boutiste.
[[nodiscard]] core::Result<void> writeCookedCollision(const std::filesystem::path& path,
                                                      const CollisionMesh& mesh,
                                                      std::uint64_t sourceHash, float maxError);

/// Relit un `.lvcol`. Échoue s'il est illisible, tronqué, d'un indice hors de ses sommets, ou
/// **périmé** : cuit par une autre version du cuiseur, depuis une autre source, ou à une autre
/// tolérance que `maxError`.
[[nodiscard]] core::Result<CollisionMesh>
readCookedCollision(const std::filesystem::path& path, std::uint64_t sourceHash, float maxError);

} // namespace levain::assets
