#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <flecs.h>

#include "levain/assets/asset_id.hpp"
#include "levain/assets/cooked_texture.hpp"
#include "levain/assets/gltf.hpp"
#include "levain/assets/registry.hpp"
#include "levain/core/error.hpp"

namespace levain::assets
{

/// Le mesh que dessine une entité : le mesh `sub` du glTF `asset`. Il se pose par `set`, jamais par
/// `get_mut` ; une entité qui en porte un ne se `clone()` pas (voir `AssetsModule`).
struct MeshRef
{
    AssetRef mesh;
};

/// Combien d'entités utilisent chaque asset, tenu à jour par les hooks du module ; et ceux qui sont
/// tombés à zéro depuis le dernier `takeUnusedAssets`.
struct AssetUsage
{
    std::map<AssetId, int> counts;
    std::set<AssetId> unused;
    /// Vrai pendant le jeu de l'éditeur (`holdAssetsInUse`) : `takeUnusedAssets` ne rend rien.
    bool held = false;
};

/// Le module flecs des assets : il pose le singleton `AssetUsage` et compte les références de
/// `MeshRef`. S'installe par `world.import<levain::assets::AssetsModule>()`.
///
/// Le comptage suit les **changements**, jamais la taille du monde (choix de Donnovan, 24/09, pour
/// viser un jour la Switch 2 ou le mobile) : aucun parcours par image. Mesuré en Release sur la
/// machine de référence, recompter chaque image coûtait 27 µs pour 10 000 entités et 273 µs pour
/// 100 000, trois à cinq fois plus sur une console portable ou un téléphone.
struct AssetsModule
{
    explicit AssetsModule(flecs::world& world);
};

/// Le nombre d'entités qui utilisent `asset`.
[[nodiscard]] int referenceCount(const flecs::world& world, AssetId asset);

/// Les assets tombés à zéro depuis l'appel précédent, et qui y sont encore : à décharger. S'appelle
/// en fin d'image, jamais au milieu d'un parcours (ADR-0019). Rend une liste vide tant que les
/// assets sont retenus (`holdAssetsInUse`) ; ils attendent alors, et sortent à la première image
/// après `releaseHeldAssets`.
[[nodiscard]] std::vector<AssetId> takeUnusedAssets(flecs::world& world);

/// Retient les assets pendant le jeu de l'éditeur : un modèle dont la dernière entité meurt en jeu
/// reste chargé jusqu'au Stop, où l'entité est réveillée. Sans cela, la fin d'image le
/// déchargerait, et l'entité réveillée serait invisible (ADR-0037, décision 5). Retient tout ce
/// qui tombe à zéro tant que `releaseHeldAssets` n'a pas été appelé, ce qui est né et mort en
/// jeu compris : il se décharge alors au Stop. Refusé, avec la raison au journal et en retour, si
/// les assets sont déjà retenus : deux Play sans Stop entre eux sont un bug de l'appelant.
[[nodiscard]] std::optional<std::string> holdAssetsInUse(AssetUsage& usage);

/// Rend les assets retenus : ceux qui sont tombés à zéro pendant ce temps sortent de
/// `takeUnusedAssets` à l'image suivante (sauf ceux qu'une entité utilise de nouveau, que le
/// comptage a déjà retirés de la liste).
void releaseHeldAssets(AssetUsage& usage);

/// Les modèles chargés en mémoire, par GUID.
struct ModelCache
{
    std::map<AssetId, Model> models;
};

/// Le modèle `asset`, chargé depuis le disque au premier appel. Un GUID inconnu du registre est un
/// échec : son `.meta` est orphelin, ou l'asset a disparu.
[[nodiscard]] core::Result<const Model*> loadModel(ModelCache& cache, const AssetRegistry& registry,
                                                   AssetId asset);

/// Une texture de couleur de base, depuis sa source : un fichier image du registre, ou une image
/// embarquée dans un modèle déjà chargé (`{GUID du modèle, indice}`). Décodée en RGBA, sans mips.
[[nodiscard]] core::Result<Image> loadTexture(const AssetRegistry& registry,
                                              const ModelCache& models, AssetRef texture);

/// Le nom de base des fichiers cuits d'une texture, dans `.cooked/` (ADR-0020) : `<guid>` pour un
/// fichier image, `<guid>.<indice>` pour une image embarquée dans un modèle, suivi de `.linear`
/// pour ses données (une normal map) : ses mips ne sont pas celles de sa version en couleur. Vide
/// si l'asset est inconnu.
[[nodiscard]] std::optional<std::filesystem::path>
cookedTextureStem(const AssetRegistry& registry, AssetRef texture,
                  ImageEncoding encoding = ImageEncoding::Srgb);

/// Une texture prête pour le GPU, au format `target`, par le chemin le plus rapide qui soit à jour
/// (ADR-0020) : le cache de la plateforme (`.bc7.ktx2`, rien à faire), sinon le maître UASTC
/// (`.ktx2`, transcodé), sinon la source (décodée, mips calculées), avec un avertissement.
/// `encoding` dit ce que portent ses octets : une couleur, ou des données (`textureUsesOf`).
[[nodiscard]] core::Result<TextureData>
loadTextureData(const AssetRegistry& registry, const ModelCache& models, AssetRef texture,
                TextureFormat target, ImageEncoding encoding = ImageEncoding::Srgb);

} // namespace levain::assets
