#include "levain/assets/collision.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include <meshoptimizer.h>

#include "levain/assets/cooked.hpp"
#include "levain/core/assert.hpp"
#include "levain/core/log.hpp"
#include "levain/scene/transform.hpp"

namespace levain::assets
{

namespace
{

/// Le feuillage : un matériau à transparence découpée (`alphaMode` `MASK`). On ne se cogne pas à
/// des feuilles, et leurs milliers de triangles coûtent cher à qui s'y appuie (ADR-0028). Une vitre
/// en `BLEND`, elle, reste solide.
bool isCutoutFoliage(const ModelMaterial& material)
{
    return material.alphaMasked;
}

/// Ce qui a une collision : un triangle rigide, hors du feuillage. Ce qui est skinné n'en a pas :
/// sa forme suit un squelette. Le modèle est valide (`whyNotAValidModel`) : son matériau existe.
bool collides(const Model& model, const MeshPrimitive& primitive)
{
    if (!primitive.joints.empty())
    {
        return false;
    }
    return !primitive.material.has_value() ||
           !isCutoutFoliage(model.materials[*primitive.material]);
}

/// Les triangles de collision du modèle, dans son repère, sommets non soudés.
CollisionMesh gatherTriangles(const Model& model)
{
    CollisionMesh mesh;
    // Les nœuds sont rangés parent avant enfant (`whyNotAValidModel`) : la matrice du parent est
    // déjà calculée. Un os, **et tout ce qui pend sous lui** (un casque, une épée), n'a pas de
    // collision : `instantiateModel` ne les affiche pas, et leur pose de repos ne veut rien dire.
    std::vector<glm::mat4> world(model.nodes.size());
    std::vector<bool> underJoint(model.nodes.size(), false);
    for (std::size_t i = 0; i < model.nodes.size(); ++i)
    {
        const ModelNode& node = model.nodes[i];
        LEVAIN_ASSERT(!node.parent || *node.parent < i, "un parent rangé après son enfant");
        const glm::mat4 local = scene::localMatrix(node.local);
        world[i] = node.parent ? scene::worldMatrix(world[*node.parent], local) : local;
        underJoint[i] = node.joint || (node.parent && underJoint[*node.parent]);
        if (underJoint[i] || !node.mesh)
        {
            continue;
        }
        for (const MeshPrimitive& primitive : model.meshes[*node.mesh].primitives)
        {
            if (!collides(model, primitive))
            {
                continue;
            }
            LEVAIN_ASSERT(mesh.vertices.size() + primitive.vertices.size() <= UINT32_MAX,
                          "plus de 4 milliards de sommets de collision");
            const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
            for (const ModelVertex& vertex : primitive.vertices)
            {
                mesh.vertices.emplace_back(world[i] * glm::vec4(vertex.position, 1.0f));
            }
            for (const std::uint32_t index : primitive.indices)
            {
                mesh.indices.push_back(base + index);
            }
        }
    }
    return mesh;
}

/// Soude les sommets de même position : ce qui sépare un cube affiché en 24 sommets (ses normales,
/// ses UV) ne compte pas pour la collision, et la simplification ne réduit rien à travers une
/// couture qu'elle prend pour un bord (meshoptimizer, « Vertex remap »). La comparaison est au bit
/// près : deux copies d'un sommet à un ulp l'une de l'autre, ou 0,0 et −0,0, restent deux sommets,
/// et bloquent la simplification à cet endroit.
void weldByPosition(CollisionMesh& mesh)
{
    std::vector<unsigned int> remap(mesh.vertices.size());
    const std::size_t unique =
        meshopt_generateVertexRemap(remap.data(), mesh.indices.data(), mesh.indices.size(),
                                    mesh.vertices.data(), mesh.vertices.size(), sizeof(glm::vec3));
    std::vector<glm::vec3> vertices(unique);
    meshopt_remapVertexBuffer(vertices.data(), mesh.vertices.data(), mesh.vertices.size(),
                              sizeof(glm::vec3), remap.data());
    std::vector<unsigned int> indices(mesh.indices.size());
    meshopt_remapIndexBuffer(indices.data(), mesh.indices.data(), mesh.indices.size(),
                             remap.data());
    mesh.vertices = std::move(vertices);
    mesh.indices.assign(indices.begin(), indices.end());
}

/// Simplifie à `maxError` près, **en unités du modèle** : sans `meshopt_SimplifyErrorAbsolute`,
/// meshoptimizer lirait l'erreur relativement à la taille du maillage (« 0.01 = 1% deformation »).
/// Aucun nombre de triangles visé : autant de triangles en moins que l'erreur le permet. Les
/// sommets qui ne servent plus partent ensuite.
void simplifyInModelUnits(CollisionMesh& mesh, float maxError)
{
    std::vector<unsigned int> simplified(mesh.indices.size());
    simplified.resize(meshopt_simplify(simplified.data(), mesh.indices.data(), mesh.indices.size(),
                                       &mesh.vertices[0].x, mesh.vertices.size(), sizeof(glm::vec3),
                                       0, maxError, meshopt_SimplifyErrorAbsolute, nullptr));
    std::vector<glm::vec3> vertices(mesh.vertices.size());
    vertices.resize(meshopt_optimizeVertexFetch(vertices.data(), simplified.data(),
                                                simplified.size(), mesh.vertices.data(),
                                                mesh.vertices.size(), sizeof(glm::vec3)));
    mesh.vertices = std::move(vertices);
    mesh.indices.assign(simplified.begin(), simplified.end());
}

} // namespace

CollisionMesh collisionMeshOf(const Model& model, float maxError)
{
    // meshoptimizer ne le vérifie qu'en Debug.
    LEVAIN_ASSERT(std::isfinite(maxError) && maxError >= 0.0f,
                  "une tolérance de collision finie et positive");
    CollisionMesh mesh = gatherTriangles(model);
    if (mesh.indices.empty())
    {
        return {};
    }
    weldByPosition(mesh);
    simplifyInModelUnits(mesh, maxError);
    return mesh;
}

CollisionMesh loadCollision(const AssetRegistry& registry, AssetId asset, const Model& model,
                            float maxError)
{
    const auto cooked = cookedPathOf(registry, asset, ".lvcol");
    const auto entry = registry.entries.find(asset);
    if (cooked && entry != registry.entries.end() && std::filesystem::exists(*cooked))
    {
        auto read = readCookedCollision(*cooked, entry->second.hash, maxError);
        if (read)
        {
            return std::move(*read);
        }
        core::log("assets", core::LogLevel::Warning, "{} ; collision simplifiée au chargement",
                  read.error().message);
    }
    else
    {
        core::log("assets", core::LogLevel::Warning,
                  "collision de {} pas cuite : simplifiée au chargement (lancer levain_cook)",
                  entry != registry.entries.end() ? entry->second.file.string() : toString(asset));
    }
    return collisionMeshOf(model, maxError);
}

} // namespace levain::assets
