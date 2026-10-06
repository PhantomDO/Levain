#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "levain/assets/asset_id.hpp"
#include "levain/assets/gltf.hpp"
#include "levain/assets/registry.hpp"

namespace levain::assets
{

/// Des triangles pour la physique : des sommets et trois indices par triangle. Ni `physics` ni
/// `assets` ne dépendent l'un de l'autre (SPECS §7) : l'application les recopie dans un
/// `physics::TriangleMesh`, d'une ligne.
struct CollisionMesh
{
    std::vector<glm::vec3> vertices;
    std::vector<std::uint32_t> indices;
};

/// L'écart toléré entre la collision et le modèle affiché, **dans les unités du modèle** : 2 cm
/// pour un modèle en mètres (ADR-0028). Du même ordre que la marge que le personnage garde à la
/// géométrie. Sponza passe de 262 267 à 32 073 triangles (`levain_cook`, sans ses matériaux en
/// `MASK`), et un pas du personnage de 850 µs à environ 47 µs en médiane (la sonde de l'ADR, non
/// versionnée : un ordre de grandeur).
inline constexpr float DefaultCollisionError = 0.02f;

/// La version de ce que rend `collisionMeshOf`, écrite dans chaque `.lvcol`. **Le piège de la
/// collision périmée** : à incrémenter à chaque changement de l'algorithme (un filtre, la soudure,
/// la simplification). Sans quoi un `.lvcol` cuit par l'ancien resterait « à jour », puisque ni la
/// source, ni le cuiseur, ni la tolérance n'ont changé.
inline constexpr std::uint32_t CollisionMeshVersion = 1;

/// La collision d'un modèle, tirée de son maillage affiché (ADR-0028) :
///
/// 1. les triangles de chaque nœud, dans le repère du modèle (les `Transform` des nœuds composés) ;
/// 2. **sans les matériaux à transparence découpée** (`ModelMaterial::alphaMasked`) : le feuillage.
///    On ne se cogne pas à des feuilles, et leurs milliers de triangles coûtent cher à qui s'y
///    appuie. Ceux en `BLEND` restent : une vitre est solide ;
/// 3. les sommets **soudés par position**, sans leurs coutures d'UV et de normales : un cube
///    affiché a 24 sommets, sa collision 8 ;
/// 4. **simplifiés** par meshoptimizer, à `maxError` près, en unités du modèle. Ce n'est pas un
///    écart maximal : meshoptimizer mesure une **moyenne pondérée par l'aire**, et un détail fin et
///    raide au milieu d'une grande surface plate peut disparaître bien au-delà de la tolérance (un
///    test le fige). La garde, ce sont les hauteurs du sol de Sponza vérifiées par rayons.
///
/// Les os, ce qui pend sous eux et ce qui est skinné n'ont pas de collision : un personnage animé a
/// la sienne, une capsule.
///
/// **Le piège de l'échelle** : la tolérance suit le modèle, puisque le cuiseur ne connaît pas son
/// placement. Posé à l'échelle s, il a une collision à s × `maxError` près ; pour 2 cm dans le
/// monde, l'appelant qui simplifie au chargement passe `DefaultCollisionError / s`. Les sommets,
/// eux, sont à mettre à l'échelle par l'appelant, puisqu'un corps n'a pas d'échelle (ADR-0026).
[[nodiscard]] CollisionMesh collisionMeshOf(const Model& model,
                                            float maxError = DefaultCollisionError);

/// La collision du modèle `asset`, déjà chargé en `model` : sa version cuite (`.lvcol`) si elle est
/// à jour, sinon calculée ici (`collisionMeshOf`), et on le signale, comme un modèle cuit retombe
/// sur son glTF (ADR-0020). Les 60 ms de la simplification de Sponza sont alors payées au
/// chargement.
[[nodiscard]] CollisionMesh loadCollision(const AssetRegistry& registry, AssetId asset,
                                          const Model& model,
                                          float maxError = DefaultCollisionError);

} // namespace levain::assets
