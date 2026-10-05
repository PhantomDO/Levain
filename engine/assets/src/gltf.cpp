#include "levain/assets/gltf.hpp"

#include <algorithm>
#include <format>
#include <numeric>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <variant>

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include "levain/assets/asset_ref.hpp"

namespace levain::assets
{

namespace
{

std::unexpected<core::Error> gltfError(const std::filesystem::path& path, std::string_view what)
{
    return core::makeError(core::ErrorCode::InvalidData,
                           std::format("{} : {}", path.string(), what));
}

/// Remplit `vertices[i].*field` avec l'attribut `name`, s'il existe. Rend faux si sa taille ne
/// correspond pas aux positions : un glTF mal formé, que fastgltf ne vérifie pas.
template <typename Element, typename Field>
bool readAttribute(const fastgltf::Asset& asset, const fastgltf::Primitive& primitive,
                   std::string_view name, std::vector<ModelVertex>& vertices,
                   Field ModelVertex::* field)
{
    const auto* attribute = primitive.findAttribute(name);
    if (attribute == primitive.attributes.end())
    {
        return true;
    }
    const fastgltf::Accessor& accessor = asset.accessors[attribute->accessorIndex];
    if (accessor.count != vertices.size())
    {
        return false;
    }
    fastgltf::iterateAccessorWithIndex<Element>(asset, accessor, [&](Element value, std::size_t i)
                                                { vertices[i].*field = value; });
    return true;
}

core::Result<MeshPrimitive> readPrimitive(const fastgltf::Asset& asset,
                                          const fastgltf::Primitive& primitive,
                                          const std::filesystem::path& path)
{
    if (primitive.type != fastgltf::PrimitiveType::Triangles)
    {
        return gltfError(path, "seuls les triangles sont pris en charge");
    }
    const auto* position = primitive.findAttribute("POSITION");
    if (position == primitive.attributes.end())
    {
        return gltfError(path, "une primitive sans POSITION");
    }

    MeshPrimitive result;
    result.material = primitive.materialIndex
                          ? std::optional{static_cast<std::uint32_t>(*primitive.materialIndex)}
                          : std::nullopt;
    const fastgltf::Accessor& positions = asset.accessors[position->accessorIndex];
    result.vertices.resize(positions.count);
    fastgltf::iterateAccessorWithIndex<glm::vec3>(asset, positions,
                                                  [&](glm::vec3 value, std::size_t i)
                                                  { result.vertices[i].position = value; });
    if (!readAttribute<glm::vec3>(asset, primitive, "NORMAL", result.vertices,
                                  &ModelVertex::normal) ||
        !readAttribute<glm::vec4>(asset, primitive, "TANGENT", result.vertices,
                                  &ModelVertex::tangent) ||
        !readAttribute<glm::vec2>(asset, primitive, "TEXCOORD_0", result.vertices,
                                  &ModelVertex::uv))
    {
        return gltfError(path, "un attribut n'a pas autant d'éléments que POSITION");
    }

    // Le skinning : les deux attributs vont ensemble, un par sommet. fastgltf convertit les indices
    // d'os (octets ou mots de 16 bits) et les poids normalisés vers nos types.
    const auto* joints = primitive.findAttribute("JOINTS_0");
    const auto* weights = primitive.findAttribute("WEIGHTS_0");
    if ((joints == primitive.attributes.end()) != (weights == primitive.attributes.end()))
    {
        return gltfError(path, "JOINTS_0 sans WEIGHTS_0, ou l'inverse");
    }
    if (joints != primitive.attributes.end())
    {
        const fastgltf::Accessor& jointAccessor = asset.accessors[joints->accessorIndex];
        const fastgltf::Accessor& weightAccessor = asset.accessors[weights->accessorIndex];
        if (jointAccessor.count != positions.count || weightAccessor.count != positions.count)
        {
            return gltfError(path, "JOINTS_0 ou WEIGHTS_0 n'a pas autant d'éléments que POSITION");
        }
        result.joints.resize(positions.count);
        result.weights.resize(positions.count);
        fastgltf::copyFromAccessor<glm::u16vec4>(asset, jointAccessor, result.joints.data());
        fastgltf::copyFromAccessor<glm::vec4>(asset, weightAccessor, result.weights.data());
    }

    // Sans indices, les sommets se lisent trois par trois : 0, 1, 2, puis 3, 4, 5…
    if (primitive.indicesAccessor)
    {
        const fastgltf::Accessor& indices = asset.accessors[*primitive.indicesAccessor];
        result.indices.resize(indices.count);
        fastgltf::copyFromAccessor<std::uint32_t>(asset, indices, result.indices.data());
    }
    else
    {
        result.indices.resize(result.vertices.size());
        std::iota(result.indices.begin(), result.indices.end(), 0u);
    }
    return result;
}

/// Décode une image glTF, où qu'elle soit : un fichier à côté du `.gltf`, des octets embarqués
/// (base64), ou une portion d'un buffer (le cas des `.glb`).
core::Result<Image> readImage(const fastgltf::Asset& asset, const fastgltf::Image& image,
                              const std::filesystem::path& path)
{
    const std::string name = std::format("{} : image « {} »", path.string(), image.name);
    return std::visit(
        fastgltf::visitor{
            [&](const fastgltf::sources::URI& uri) -> core::Result<Image>
            {
                if (!uri.uri.isLocalPath())
                {
                    return gltfError(path, "image hors du disque (URI distante)");
                }
                return loadImage(path.parent_path() / uri.uri.fspath());
            },
            [&](const fastgltf::sources::Array& array) -> core::Result<Image>
            { return decodeImage(std::span{array.bytes.data(), array.bytes.size()}, name); },
            [&](const fastgltf::sources::BufferView& view) -> core::Result<Image>
            {
                const fastgltf::BufferView& bufferView = asset.bufferViews[view.bufferViewIndex];
                const auto* bytes = std::get_if<fastgltf::sources::Array>(
                    &asset.buffers[bufferView.bufferIndex].data);
                if (bytes == nullptr)
                {
                    return gltfError(path, "image dans un buffer non chargé");
                }
                return decodeImage(
                    std::span{bytes->bytes.data() + bufferView.byteOffset, bufferView.byteLength},
                    name);
            },
            [&](const auto&) -> core::Result<Image>
            { return gltfError(path, "source d'image non prise en charge"); }},
        image.data);
}

/// La référence d'une image glTF (ADR-0020) : le GUID de son fichier, s'il en a un, sinon `{GUID du
/// modèle, indice}`, et l'image est alors décodée dans `Model::embeddedImages`.
core::Result<AssetRef> imageRef(const fastgltf::Asset& asset, std::size_t index,
                                const std::filesystem::path& path, AssetId self,
                                const AssetRegistry& registry, Model& model)
{
    const fastgltf::Image& image = asset.images[index];
    if (const auto* uri = std::get_if<fastgltf::sources::URI>(&image.data);
        uri != nullptr && uri->uri.isLocalPath())
    {
        const std::filesystem::path file = path.parent_path() / uri->uri.fspath();
        const auto id = idOf(registry, file);
        if (!id)
        {
            return gltfError(path,
                             std::format("image {} hors du registre : est-elle sous une racine "
                                         "d'assets ?",
                                         file.string()));
        }
        return AssetRef{.asset = *id, .sub = 0};
    }
    const auto sub = static_cast<std::uint32_t>(index);
    if (!model.embeddedImages.contains(sub))
    {
        auto decoded = readImage(asset, image, path);
        if (!decoded)
        {
            return std::unexpected(decoded.error());
        }
        model.embeddedImages.emplace(sub, std::move(*decoded));
    }
    return AssetRef{.asset = self, .sub = sub};
}

/// La référence de l'image d'une texture glTF, ou rien si le matériau n'en a pas (ou une texture
/// sans image, qu'une extension fournirait).
/// `OptionalTextureInfo` : l'optionnel de fastgltf, sur `TextureInfo` ou `NormalTextureInfo`.
template <typename OptionalTextureInfo>
core::Result<std::optional<AssetRef>> textureRef(const fastgltf::Asset& asset,
                                                 const OptionalTextureInfo& texture,
                                                 const std::filesystem::path& path, AssetId self,
                                                 const AssetRegistry& registry, Model& model)
{
    if (!texture || !asset.textures[texture->textureIndex].imageIndex)
    {
        return std::nullopt;
    }
    auto ref = imageRef(asset, *asset.textures[texture->textureIndex].imageIndex, path, self,
                        registry, model);
    if (!ref)
    {
        return std::unexpected(ref.error());
    }
    return *ref;
}

/// Les matériaux metallic-roughness : leurs facteurs, et les références de leurs textures.
core::Result<void> readMaterials(const fastgltf::Asset& asset, const std::filesystem::path& path,
                                 AssetId self, const AssetRegistry& registry, Model& model)
{
    for (const fastgltf::Material& material : asset.materials)
    {
        const fastgltf::PBRData& pbr = material.pbrData;
        auto baseColor = textureRef(asset, pbr.baseColorTexture, path, self, registry, model);
        auto metallicRoughness =
            textureRef(asset, pbr.metallicRoughnessTexture, path, self, registry, model);
        auto normal = textureRef(asset, material.normalTexture, path, self, registry, model);
        if (!baseColor || !metallicRoughness || !normal)
        {
            return std::unexpected(!baseColor           ? baseColor.error()
                                   : !metallicRoughness ? metallicRoughness.error()
                                                        : normal.error());
        }
        const auto& factor = pbr.baseColorFactor;
        model.materials.push_back(ModelMaterial{
            .baseColorFactor = {factor.x(), factor.y(), factor.z(), factor.w()},
            .baseColorTexture = *baseColor,
            .metallicFactor = pbr.metallicFactor,
            .roughnessFactor = pbr.roughnessFactor,
            .metallicRoughnessTexture = *metallicRoughness,
            .normalTexture = *normal,
            .normalScale = material.normalTexture ? material.normalTexture->scale : 1.0f,
            .alphaMasked = material.alphaMode == fastgltf::AlphaMode::Mask,
        });
    }
    return {};
}

/// Le `Transform` d'un nœud. `DecomposeNodeMatrices` garantit la forme TRS : une matrice glTF ne
/// peut ni cisailler ni projeter, elle se décompose toujours.
scene::Transform localTransformOf(const fastgltf::Node& node)
{
    const auto& trs = std::get<fastgltf::TRS>(node.transform);
    // fastgltf range un quaternion en (x, y, z, w), glm le construit en (w, x, y, z).
    return scene::Transform{
        .position = {trs.translation.x(), trs.translation.y(), trs.translation.z()},
        .rotation =
            glm::quat{trs.rotation.w(), trs.rotation.x(), trs.rotation.y(), trs.rotation.z()},
        .scale = {trs.scale.x(), trs.scale.y(), trs.scale.z()}};
}

/// Ajoute `nodeIndex` puis ses descendants, parent d'abord : l'ordre que promet `Model::nodes`.
void appendNode(const fastgltf::Asset& asset, std::size_t nodeIndex,
                std::optional<std::uint32_t> parent, const std::set<std::size_t>& joints,
                Model& model)
{
    const fastgltf::Node& node = asset.nodes[nodeIndex];
    const auto self = static_cast<std::uint32_t>(model.nodes.size());
    model.nodes.push_back(ModelNode{
        .name = std::string{node.name},
        .local = localTransformOf(node),
        .mesh = node.meshIndex ? std::optional{static_cast<std::uint32_t>(*node.meshIndex)}
                               : std::nullopt,
        .parent = parent,
        .joint = joints.contains(nodeIndex)});
    for (const std::size_t child : node.children)
    {
        appendNode(asset, child, self, joints, model);
    }
}

} // namespace

core::Result<Model> loadGltf(const std::filesystem::path& path, AssetId self,
                             const AssetRegistry& registry)
{
    auto data = fastgltf::GltfDataBuffer::FromPath(path);
    if (data.error() != fastgltf::Error::None)
    {
        return core::makeError(
            core::ErrorCode::FileNotFound,
            std::format("{} : {}", path.string(), fastgltf::getErrorMessage(data.error())));
    }

    fastgltf::Parser parser;
    auto asset = parser.loadGltf(data.get(), path.parent_path(),
                                 fastgltf::Options::LoadExternalBuffers |
                                     fastgltf::Options::DecomposeNodeMatrices);
    if (asset.error() != fastgltf::Error::None)
    {
        return gltfError(path, fastgltf::getErrorMessage(asset.error()));
    }

    Model model;
    if (auto materials = readMaterials(asset.get(), path, self, registry, model); !materials)
    {
        return std::unexpected(materials.error());
    }
    for (const fastgltf::Mesh& mesh : asset->meshes)
    {
        ModelMesh& out =
            model.meshes.emplace_back(ModelMesh{.name = std::string{mesh.name}, .primitives = {}});
        for (const fastgltf::Primitive& primitive : mesh.primitives)
        {
            auto read = readPrimitive(asset.get(), primitive, path);
            if (!read)
            {
                return std::unexpected(read.error());
            }
            out.primitives.push_back(std::move(*read));
        }
    }

    // La scène par défaut, ou la première ; un glTF sans scène n'a rien à montrer.
    const std::size_t sceneIndex = asset->defaultScene.value_or(0);
    if (sceneIndex >= asset->scenes.size())
    {
        return gltfError(path, "aucune scène");
    }
    std::set<std::size_t> joints;
    for (const fastgltf::Skin& skin : asset->skins)
    {
        joints.insert(skin.joints.begin(), skin.joints.end());
    }
    for (const std::size_t root : asset->scenes[sceneIndex].nodeIndices)
    {
        appendNode(asset.get(), root, std::nullopt, joints, model);
    }
    if (const auto reason = whyNotAValidModel(model))
    {
        return gltfError(path, *reason);
    }
    return model;
}

std::optional<std::string> whyNotAValidModel(const Model& model)
{
    for (const ModelMesh& mesh : model.meshes)
    {
        for (const MeshPrimitive& primitive : mesh.primitives)
        {
            if (primitive.indices.size() % 3 != 0)
            {
                return std::format("{} : un nombre d'indices qui n'est pas un multiple de 3",
                                   mesh.name);
            }
            if (std::ranges::any_of(primitive.indices, [&](std::uint32_t index)
                                    { return index >= primitive.vertices.size(); }))
            {
                return std::format("{} : un indice au-delà des sommets de sa primitive", mesh.name);
            }
            if (primitive.material && *primitive.material >= model.materials.size())
            {
                return std::format("{} : un matériau qui n'existe pas", mesh.name);
            }
            if (!primitive.joints.empty() &&
                (primitive.joints.size() != primitive.vertices.size() ||
                 primitive.weights.size() != primitive.vertices.size()))
            {
                return std::format("{} : des os ou des poids qui ne vont pas un par sommet",
                                   mesh.name);
            }
        }
    }
    for (std::size_t i = 0; i < model.nodes.size(); ++i)
    {
        const ModelNode& node = model.nodes[i];
        if (node.mesh && *node.mesh >= model.meshes.size())
        {
            return std::format("nœud {} : un mesh qui n'existe pas", node.name);
        }
        if (node.parent && *node.parent >= i)
        {
            return std::format("nœud {} : un parent qui n'est pas rangé avant lui", node.name);
        }
    }
    return std::nullopt;
}

flecs::entity instantiateModel(flecs::world& world, const Model& model, AssetId asset,
                               std::string_view rootName)
{
    const flecs::entity root = world.entity(std::string{rootName}.c_str()).set(scene::Transform{});
    std::vector<flecs::entity> entities;
    entities.reserve(model.nodes.size());
    for (const ModelNode& node : model.nodes)
    {
        // Un os, ou ce qui y est accroché, reste une donnée de la pose (ADR-0022) : une entité
        // vide marque sa place, et ses enfants la sautent aussi.
        if (node.joint || (node.parent && !entities[*node.parent]))
        {
            entities.emplace_back();
            continue;
        }
        // Les nœuds sont rangés parent d'abord : l'entité du parent existe déjà. Les noms glTF
        // ne sont pas uniques ; une entité anonyme évite qu'un doublon en écrase un autre.
        const flecs::entity parent = node.parent ? entities[*node.parent] : root;
        flecs::entity entity = world.entity(flecs::Parent{parent}).set(node.local);
        if (node.mesh)
        {
            entity.set(MeshRef{.mesh = {.asset = asset, .sub = *node.mesh}});
        }
        entities.push_back(entity);
    }
    return root;
}

std::vector<std::pair<AssetRef, ImageEncoding>> textureUsesOf(const Model& model)
{
    std::vector<std::pair<AssetRef, ImageEncoding>> uses;
    for (const ModelMaterial& material : model.materials)
    {
        for (const auto& [texture, encoding] :
             {std::pair{material.baseColorTexture, ImageEncoding::Srgb},
              std::pair{material.metallicRoughnessTexture, ImageEncoding::Linear},
              std::pair{material.normalTexture, ImageEncoding::Linear}})
        {
            if (texture)
            {
                uses.emplace_back(*texture, encoding);
            }
        }
    }
    return uses;
}

} // namespace levain::assets
