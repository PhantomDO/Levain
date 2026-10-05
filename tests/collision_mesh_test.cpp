// La collision d'un modèle tirée de son maillage affiché (ADR-0028) : les nœuds composés, le
// feuillage écarté, les sommets soudés, la simplification à erreur bornée. Des modèles construits à
// la main : la mesure sur Sponza est dans le sandbox et la CI, qui ont les assets.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <doctest/doctest.h>
#include <glm/glm.hpp>

#include "levain/assets/collision.hpp"
#include "levain/assets/gltf.hpp"

using levain::assets::CollisionMesh;
using levain::assets::MeshPrimitive;
using levain::assets::Model;

namespace
{

/// Un plan carré de `size` mètres à y = 0, découpé en `cells` × `cells` cases : beaucoup de
/// triangles pour une surface que deux suffisent à décrire. Chaque case a ses quatre sommets, comme
/// un maillage affiché dont chaque case aurait ses UV : la soudure doit les réunir.
MeshPrimitive finePlane(float size, int cells)
{
    MeshPrimitive plane;
    const float step = size / static_cast<float>(cells);
    for (int z = 0; z < cells; ++z)
    {
        for (int x = 0; x < cells; ++x)
        {
            const auto base = static_cast<std::uint32_t>(plane.vertices.size());
            const float x0 = -size / 2.0f + step * static_cast<float>(x);
            const float z0 = -size / 2.0f + step * static_cast<float>(z);
            for (const glm::vec2 corner :
                 {glm::vec2{0, 0}, glm::vec2{1, 0}, glm::vec2{1, 1}, glm::vec2{0, 1}})
            {
                plane.vertices.push_back(
                    {.position = {x0 + corner.x * step, 0.0f, z0 + corner.y * step}});
            }
            // Deux triangles tournés vers le haut (sens trigonométrique vu d'en haut).
            for (const std::uint32_t index : {0u, 2u, 1u, 0u, 3u, 2u})
            {
                plane.indices.push_back(base + index);
            }
        }
    }
    return plane;
}

/// Un modèle d'un seul nœud, à l'origine, qui porte ces primitives.
Model modelOf(std::vector<MeshPrimitive> primitives)
{
    Model model;
    model.meshes.push_back({.name = "décor", .primitives = std::move(primitives)});
    levain::assets::ModelNode& root = model.nodes.emplace_back();
    root.name = "racine";
    root.mesh = 0u;
    return model;
}

std::size_t triangleCount(const CollisionMesh& mesh)
{
    return mesh.indices.size() / 3;
}

float highest(const CollisionMesh& mesh)
{
    float y = -INFINITY;
    for (const glm::vec3& vertex : mesh.vertices)
    {
        y = std::max(y, vertex.y);
    }
    return y;
}

} // namespace

TEST_CASE("un plan découpé en 800 triangles redevient une poignée, et reste à plat")
{
    const CollisionMesh mesh = levain::assets::collisionMeshOf(modelOf({finePlane(10.0f, 20)}));

    REQUIRE(!mesh.indices.empty());
    CHECK(triangleCount(mesh) <= 8);
    // À plat, au centimètre près : la simplification n'a rien soulevé.
    for (const glm::vec3& vertex : mesh.vertices)
    {
        CHECK(std::abs(vertex.y) < 1e-4f);
    }
    // Les coins restent : la surface couverte n'a pas rétréci.
    const auto [minX, maxX] = std::ranges::minmax(mesh.vertices, {}, &glm::vec3::x);
    CHECK(minX.x == doctest::Approx(-5.0f));
    CHECK(maxX.x == doctest::Approx(5.0f));
    // Les sommets inutiles sont partis avec leurs triangles.
    CHECK(mesh.vertices.size() <= 3 * triangleCount(mesh));
}

TEST_CASE(
    "une bosse douce plus haute que la tolérance survit à la simplification, une plus basse non")
{
    const auto bumpTop = [](float height)
    {
        MeshPrimitive plane = finePlane(10.0f, 20);
        // Le sommet commun aux quatre cases du centre, levé de `height` : une bosse de 1 m de
        // large.
        for (levain::assets::ModelVertex& vertex : plane.vertices)
        {
            if (glm::length(glm::vec2{vertex.position.x, vertex.position.z}) < 1e-4f)
            {
                vertex.position.y = height;
            }
        }
        const CollisionMesh mesh = levain::assets::collisionMeshOf(modelOf({plane}));
        REQUIRE(!mesh.vertices.empty());
        return highest(mesh);
    };
    CHECK(bumpTop(0.10f) == doctest::Approx(0.10f)); // 10 cm : au-delà des 2 cm, gardée
    CHECK(bumpTop(0.005f) < 0.001f);                 // 5 mm : en deçà, aplanie
}

// La tolérance n'est pas un écart maximal : meshoptimizer mesure une moyenne pondérée par l'aire.
// Un pic fin et raide, au milieu d'une grande surface plate, disparaît bien au-delà des 2 cm. Ce
// test fige cette limite : s'il échoue, la simplification est devenue plus prudente, et le
// commentaire de `collisionMeshOf` est à revoir.
TEST_CASE("un pic fin et raide de 10 cm disparaît, bien au-delà de la tolérance")
{
    MeshPrimitive plane = finePlane(1.0f, 16); // des cases de 6,25 cm
    for (levain::assets::ModelVertex& vertex : plane.vertices)
    {
        if (glm::length(glm::vec2{vertex.position.x, vertex.position.z}) < 1e-4f)
        {
            vertex.position.y = 0.10f;
        }
    }
    const CollisionMesh mesh = levain::assets::collisionMeshOf(modelOf({plane}));
    REQUIRE(!mesh.vertices.empty());
    CHECK(highest(mesh) < 0.001f);
}

TEST_CASE("les nœuds composent leurs Transform : la collision est dans le repère du modèle")
{
    Model model = modelOf({finePlane(2.0f, 2)});
    // Le plan devient l'enfant d'une racine montée de 3 m et doublée, lui-même monté de 1 m : 5 m.
    // L'échelle vérifie l'ordre de la composition, qu'une translation seule ne verrait pas.
    model.nodes[0].mesh.reset();
    model.nodes[0].local.position = {0.0f, 3.0f, 0.0f};
    model.nodes[0].local.scale = glm::vec3{2.0f};
    levain::assets::ModelNode& child = model.nodes.emplace_back();
    child.name = "enfant";
    child.mesh = 0u;
    child.parent = 0u;
    child.local.position = {0.0f, 1.0f, 0.0f};

    const CollisionMesh mesh = levain::assets::collisionMeshOf(model);
    REQUIRE(!mesh.vertices.empty());
    for (const glm::vec3& vertex : mesh.vertices)
    {
        CHECK(vertex.y == doctest::Approx(5.0f));
    }
}

TEST_CASE("un os, et ce qui pend sous lui, n'ont pas de collision, comme ils ne sont pas affichés")
{
    Model model = modelOf({finePlane(10.0f, 4)});
    MeshPrimitive helmet = finePlane(1.0f, 2);
    for (levain::assets::ModelVertex& vertex : helmet.vertices)
    {
        vertex.position.y = 2.0f;
    }
    model.meshes.push_back({.name = "casque", .primitives = {helmet}});
    levain::assets::ModelNode& bone = model.nodes.emplace_back();
    bone.name = "tête";
    bone.joint = true;
    levain::assets::ModelNode& hat = model.nodes.emplace_back();
    hat.name = "casque";
    hat.mesh = 1u;
    hat.parent = 1u; // sous l'os, sans en être un

    const CollisionMesh mesh = levain::assets::collisionMeshOf(model);
    REQUIRE(!mesh.vertices.empty());
    CHECK(highest(mesh) == doctest::Approx(0.0f)); // seul le sol reste
}

TEST_CASE("un modèle incohérent est refusé, avec sa raison, avant qu'une bibliothèque ne le lise")
{
    using levain::assets::whyNotAValidModel;
    CHECK_FALSE(whyNotAValidModel(modelOf({finePlane(2.0f, 2)})).has_value());

    MeshPrimitive outOfRange = finePlane(2.0f, 2);
    outOfRange.indices[0] = static_cast<std::uint32_t>(outOfRange.vertices.size());
    CHECK(whyNotAValidModel(modelOf({outOfRange})).has_value());

    MeshPrimitive partial = finePlane(2.0f, 2);
    partial.indices.pop_back();
    CHECK(whyNotAValidModel(modelOf({partial})).has_value());

    MeshPrimitive missingMaterial = finePlane(2.0f, 2);
    missingMaterial.material = 3u;
    CHECK(whyNotAValidModel(modelOf({missingMaterial})).has_value());

    Model orphan = modelOf({finePlane(2.0f, 2)});
    orphan.nodes[0].parent = 0u; // son propre parent
    CHECK(whyNotAValidModel(orphan).has_value());

    Model noMesh = modelOf({finePlane(2.0f, 2)});
    noMesh.nodes[0].mesh = 5u;
    CHECK(whyNotAValidModel(noMesh).has_value());
}

TEST_CASE("le feuillage en transparence découpée et ce qui est skinné n'ont pas de collision")
{
    MeshPrimitive leaves = finePlane(2.0f, 4);
    leaves.material = 0u;
    for (levain::assets::ModelVertex& vertex : leaves.vertices)
    {
        vertex.position.y += 5.0f; // au-dessus du sol : on verrait s'il restait
    }
    MeshPrimitive skinned = finePlane(2.0f, 4);
    skinned.joints.assign(skinned.vertices.size(), glm::u16vec4{0});
    skinned.weights.assign(skinned.vertices.size(), glm::vec4{1.0f, 0.0f, 0.0f, 0.0f});
    for (levain::assets::ModelVertex& vertex : skinned.vertices)
    {
        vertex.position.y += 9.0f;
    }
    Model model = modelOf({finePlane(10.0f, 4), leaves, skinned});
    model.materials.emplace_back().alphaMasked = true;

    const CollisionMesh mesh = levain::assets::collisionMeshOf(model);
    REQUIRE(!mesh.vertices.empty());
    CHECK(highest(mesh) == doctest::Approx(0.0f)); // seul le sol reste

    // Le même feuillage, opaque cette fois : il compte.
    model.materials[0].alphaMasked = false;
    CHECK(highest(levain::assets::collisionMeshOf(model)) == doctest::Approx(5.0f));
}

TEST_CASE("un modèle sans triangle qui collisionne rend une collision vide")
{
    CHECK(levain::assets::collisionMeshOf(Model{}).indices.empty());
    MeshPrimitive leaves = finePlane(2.0f, 2);
    leaves.material = 0u;
    Model model = modelOf({leaves});
    model.materials.emplace_back().alphaMasked = true;
    const CollisionMesh mesh = levain::assets::collisionMeshOf(model);
    CHECK(mesh.indices.empty());
    CHECK(mesh.vertices.empty());
}
