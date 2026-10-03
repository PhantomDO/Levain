#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <flecs.h>
#include <glm/glm.hpp>

#include "levain/assets/asset_id.hpp"
#include "levain/assets/image.hpp"
#include "levain/assets/registry.hpp"
#include "levain/core/error.hpp"
#include "levain/scene/components.hpp"

namespace levain::assets
{

/// Un sommet tel que glTF le décrit. Les normales, les tangentes et les coordonnées de texture sont
/// facultatives dans un glTF : absentes, elles valent (0, 1, 0), (1, 0, 0, 1) et (0, 0).
struct ModelVertex
{
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    /// La direction où u croît, pour les normal maps (M5.1) ; w (±1) dit dans quel sens croît v.
    glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
    glm::vec2 uv{0.0f}; ///< (0, 0) en haut à gauche, comme chez nous : aucune inversion.
};

/// Une *primitive* glTF : des triangles indexés, qui partagent un matériau. Un mesh glTF en a une
/// ou plusieurs.
struct MeshPrimitive
{
    std::vector<ModelVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::optional<std::uint32_t> material; ///< Indice dans `Model::materials`.
    /// Le skinning (ADR-0022), vide pour un mesh rigide : les quatre os qui influencent chaque
    /// sommet, par leur indice dans le skin (`JOINTS_0`), et leurs poids (`WEIGHTS_0`).
    std::vector<glm::u16vec4> joints;
    std::vector<glm::vec4> weights;
};

/// Un matériau glTF, au modèle metallic-roughness (M5.1). Les textures se désignent par référence
/// (ADR-0020) : le GUID de leur fichier image, ou `{GUID du modèle, indice}` si elles sont
/// embarquées dans le glTF. Elles se chargent à part (`loadTexture`). Les facteurs multiplient leur
/// texture, ou valent seuls si elle est absente ; leurs défauts sont ceux de glTF.
struct ModelMaterial
{
    glm::vec4 baseColorFactor{1.0f};
    std::optional<AssetRef> baseColorTexture; ///< En sRGB.
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    /// La rugosité dans le vert, le métal dans le bleu : des données, pas des couleurs.
    std::optional<AssetRef> metallicRoughnessTexture;
    /// Une normal map dans l'espace tangent (la tangente des sommets) : des données elle aussi.
    std::optional<AssetRef> normalTexture;
    float normalScale = 1.0f; ///< Multiplie x et y de la normal map.
};

struct ModelMesh
{
    std::string name;
    std::vector<MeshPrimitive> primitives;
};

/// Un nœud de la scène glTF, **parent avant enfants** : `parent` désigne toujours un nœud déjà vu.
struct ModelNode
{
    std::string name;
    scene::Transform local; ///< Relatif au parent, comme le `Transform` du moteur.
    std::optional<std::uint32_t> mesh;
    std::optional<std::uint32_t> parent;
    /// Un os d'un skin : la pose l'anime (ADR-0022), et ni lui ni ses descendants ne deviennent
    /// des entités.
    bool joint = false;
};

/// Un fichier glTF lu en mémoire, sans GPU ni monde flecs : il se teste seul, et la cuisson des
/// assets (M4.3) pourra repartir de lui.
struct Model
{
    std::vector<ModelMesh> meshes;
    std::vector<ModelNode> nodes;
    std::vector<ModelMaterial> materials;
    /// Les images **embarquées** (base64, `.glb`) des matériaux, décodées, par leur indice glTF :
    /// leur référence est `{GUID du modèle, indice}`. Une image qui a son propre fichier est un
    /// asset à part, chargé par son GUID.
    std::map<std::uint32_t, Image> embeddedImages;
};

/// Lit un `.gltf` (et ses `.bin`) ou un `.glb`, avec fastgltf. `self` est le GUID du modèle, et
/// `registry` donne celui des images qu'il désigne par leur chemin : une image hors du registre
/// (hors de toute racine d'assets) est un échec. Seule la scène par défaut
/// est gardée ; seuls les triangles sont acceptés. Un fichier illisible ou incomplet est un échec
/// récupérable (ADR-0008).
[[nodiscard]] core::Result<Model> loadGltf(const std::filesystem::path& path, AssetId self,
                                           const AssetRegistry& registry);

/// Chaque texture des matériaux de `model`, avec l'encodage de ses octets : la couleur de base en
/// sRGB, la rugosité-métal et la normal map en données (`ImageEncoding::Linear`), dont les mips se
/// moyennent sur les octets. Une image qui sert aux deux usages paraît deux fois. C'est ce qui dit
/// au cuiseur comment cuire chaque image.
[[nodiscard]] std::vector<std::pair<AssetRef, ImageEncoding>> textureUsesOf(const Model& model);

/// Crée une entité par nœud qui n'est pas un os, avec son `Transform` et sa hiérarchie
/// (`flecs::Parent`, ADR-0015), sous une entité racine nommée `rootName`, que l'on déplace pour
/// déplacer tout le modèle. Un nœud qui porte un mesh reçoit un `MeshRef` vers le mesh de l'asset
/// `asset` (ADR-0019) : le modèle compte alors une référence de plus, et reste chargé tant qu'il en
/// a.
///
/// Le monde doit avoir importé `AssetsModule` (`asset_ref.hpp`).
flecs::entity instantiateModel(flecs::world& world, const Model& model, AssetId asset,
                               std::string_view rootName);

} // namespace levain::assets
