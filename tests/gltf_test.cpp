#include <filesystem>

#include <doctest/doctest.h>
#include <flecs.h>

#include "levain/assets/asset_ref.hpp"
#include "levain/assets/gltf.hpp"
#include "levain/assets/registry.hpp"
#include "levain/scene/components.hpp"
#include "levain/scene/fixed_step.hpp"
#include "levain/scene/scene.hpp"
#include "levain/scene/transform.hpp"

using levain::assets::loadGltf;
using levain::assets::MeshRef;
using levain::core::ErrorCode;

namespace
{

/// Le GUID donné au modèle de test : ses images embarquées se désignent par lui.
constexpr levain::assets::AssetId TestModelId{.high = 1, .low = 2};

/// Un triangle, et deux nœuds qui le portent : « parent », décalé de 1 en x, et son « enfant »,
/// donné par une **matrice** (échelle 2, 2 plus haut) pour vérifier la décomposition en TRS. Une
/// fonction et non une constante : construire un chemin peut lever, et une globale ne le rattrape
/// pas.
std::filesystem::path twoNodes()
{
    return std::filesystem::path{LEVAIN_TEST_DATA_DIR} / "two-nodes.gltf";
}

/// Un modèle de test, sans registre : ses images sont embarquées, il n'en a pas besoin.
levain::core::Result<levain::assets::Model> loadTestModel(const std::filesystem::path& path)
{
    return loadGltf(path, TestModelId, levain::assets::AssetRegistry{});
}

} // namespace

TEST_CASE("loadGltf lit les meshes et les nœuds, parent avant enfant")
{
    const auto model = loadTestModel(twoNodes());
    INFO("message d'erreur : " << (model ? std::string{} : model.error().message));
    REQUIRE(model.has_value());

    REQUIRE(model->meshes.size() == 1);
    REQUIRE(model->meshes[0].primitives.size() == 1);
    const auto& triangle = model->meshes[0].primitives[0];
    CHECK(triangle.vertices.size() == 3);
    CHECK(triangle.indices == std::vector<std::uint32_t>{0, 1, 2});
    CHECK(triangle.vertices[1].position.x == doctest::Approx(1.0f));
    CHECK(triangle.vertices[0].normal.y == doctest::Approx(1.0f));            // absente du fichier
    CHECK(triangle.vertices[0].tangent == glm::vec4{1.0f, 0.0f, 0.0f, 1.0f}); // absente aussi

    REQUIRE(model->nodes.size() == 2);
    CHECK(model->nodes[0].name == "parent");
    CHECK_FALSE(model->nodes[0].parent.has_value());
    CHECK(model->nodes[1].parent == 0u);
    CHECK(model->nodes[1].local.position.y == doctest::Approx(2.0f));
    CHECK(model->nodes[1].local.scale.x == doctest::Approx(2.0f));
}

TEST_CASE("loadGltf lit la couleur de base des matériaux, texture embarquée comprise")
{
    const auto model = loadTestModel(twoNodes());
    REQUIRE(model.has_value());

    CHECK(model->meshes[0].primitives[0].material == 0u);
    REQUIRE(model->materials.size() == 1);
    CHECK(model->materials[0].baseColorFactor.g == doctest::Approx(0.5f));
    // Embarquée, l'image se désigne par le GUID du modèle et son indice (ADR-0020).
    REQUIRE(model->materials[0].baseColorTexture ==
            levain::assets::AssetRef{.asset = TestModelId, .sub = 0});
    // Un PNG de 2 × 1 en base64 dans le fichier : un pixel rouge, un pixel bleu.
    REQUIRE(model->embeddedImages.contains(0));
    CHECK(model->embeddedImages.at(0).width == 2);
    CHECK(model->embeddedImages.at(0).rgba ==
          std::vector<std::uint8_t>{255, 0, 0, 255, 0, 0, 255, 255});
}

TEST_CASE("un modèle instancié garde sa hiérarchie : l'enfant suit son parent")
{
    const auto model = loadTestModel(twoNodes());
    REQUIRE(model.has_value());
    flecs::world world;
    world.import<levain::scene::SceneModule>();
    world.import<levain::assets::AssetsModule>();

    const flecs::entity root =
        levain::assets::instantiateModel(world, *model, TestModelId, "modele");
    root.set(levain::scene::Transform{.position = {0.0f, 0.0f, 10.0f}});
    levain::scene::FixedStep step;
    levain::scene::advanceWorld(world, step, 0.0f);

    int meshes = 0;
    glm::vec3 childPosition{0.0f};
    world.each(
        [&](const MeshRef&, const levain::scene::WorldTransform& world,
            const levain::scene::Transform& local)
        {
            ++meshes;
            if (local.scale.x > 1.5f) // l'enfant
            {
                childPosition = levain::scene::worldPosition(world);
            }
        });
    CHECK(meshes == 2);
    // Racine (0, 0, 10) + parent (1, 0, 0) + enfant (0, 2, 0).
    CHECK(childPosition.x == doctest::Approx(1.0f));
    CHECK(childPosition.y == doctest::Approx(2.0f));
    CHECK(childPosition.z == doctest::Approx(10.0f));
}

TEST_CASE("loadGltf signale un fichier absent, et refuse autre chose que des triangles")
{
    const auto missing = loadTestModel("introuvable.gltf");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == ErrorCode::FileNotFound);

    const auto lines = loadTestModel(std::filesystem::path{LEVAIN_TEST_DATA_DIR} / "lines.gltf");
    REQUIRE_FALSE(lines.has_value());
    CHECK(lines.error().code == ErrorCode::InvalidData);
}

TEST_CASE("une image qui a son propre fichier se désigne par son GUID, et doit être au registre")
{
    const std::filesystem::path data{LEVAIN_TEST_DATA_DIR};
    CHECK_FALSE(loadTestModel(data / "external-image.gltf").has_value()); // image hors registre

    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("levain-gltf-" + levain::assets::toString(levain::assets::generateAssetId()));
    std::filesystem::create_directories(root);
    std::filesystem::copy_file(data / "external-image.gltf", root / "external-image.gltf");
    std::filesystem::copy_file(data / "rgbw-2x2.png", root / "rgbw-2x2.png");
    levain::assets::AssetRegistry registry;
    REQUIRE(levain::assets::scanAssets(root, registry).has_value());

    levain::assets::ModelCache cache;
    const auto modelId = levain::assets::idOf(registry, root / "external-image.gltf");
    const auto imageId = levain::assets::idOf(registry, root / "rgbw-2x2.png");
    REQUIRE(modelId.has_value());
    REQUIRE(imageId.has_value());
    const auto model = levain::assets::loadModel(cache, registry, modelId.value_or(TestModelId));
    REQUIRE(model.has_value());
    REQUIRE((*model)->materials[0].baseColorTexture.has_value());
    const levain::assets::AssetRef texture =
        (*model)->materials[0].baseColorTexture.value_or(levain::assets::AssetRef{});
    CHECK(texture.asset == imageId);
    CHECK((*model)->embeddedImages.empty());

    const auto image = levain::assets::loadTexture(registry, cache, texture);
    REQUIRE(image.has_value());
    CHECK(image->width == 2);
    std::filesystem::remove_all(root);
}

TEST_CASE("un mesh skinné garde ses os et ses poids, et les os ne deviennent pas des entités")
{
    // tests/data/two-joints.gltf : un triangle « peau », lié aux os « racine » et « bout » (voir
    // animation_test.cpp).
    const auto model =
        loadTestModel(std::filesystem::path{LEVAIN_TEST_DATA_DIR} / "two-joints.gltf");
    REQUIRE(model.has_value());
    const levain::assets::MeshPrimitive& skin = model->meshes[0].primitives[0];
    REQUIRE(skin.joints.size() == 3);
    REQUIRE(skin.weights.size() == 3);
    CHECK(skin.joints[2] == glm::u16vec4{0, 1, 0, 0}); // lu depuis des octets
    CHECK(skin.weights[2].x == doctest::Approx(0.75f));

    flecs::world world;
    world.import<levain::scene::SceneModule>();
    world.import<levain::assets::AssetsModule>();
    levain::assets::instantiateModel(world, *model, TestModelId, "modele");
    int transforms = 0;
    world.each([&](const levain::scene::Transform&) { ++transforms; });
    // La racine du modèle, le porteur et la peau ; ni « racine » ni « bout ».
    CHECK(transforms == 3);
}

TEST_CASE("loadGltf lit les tangentes, avec le sens de v")
{
    const auto model = loadTestModel(std::filesystem::path{LEVAIN_TEST_DATA_DIR} / "tangents.gltf");
    INFO("message d'erreur : " << (model ? std::string{} : model.error().message));
    REQUIRE(model.has_value());
    const auto& vertices = model->meshes[0].primitives[0].vertices;
    REQUIRE(vertices.size() == 3);
    CHECK(vertices[2].tangent == glm::vec4{0.0f, 0.0f, -1.0f, -1.0f});
}

TEST_CASE("loadGltf lit les matériaux metallic-roughness et leurs textures de données")
{
    const auto model =
        loadTestModel(std::filesystem::path{LEVAIN_TEST_DATA_DIR} / "materials.gltf");
    INFO("message d'erreur : " << (model ? std::string{} : model.error().message));
    REQUIRE(model.has_value());
    REQUIRE(model->materials.size() == 1);
    const levain::assets::ModelMaterial& material = model->materials[0];
    CHECK(material.metallicFactor == doctest::Approx(0.25f));
    CHECK(material.roughnessFactor == doctest::Approx(0.75f));
    CHECK(material.normalScale == doctest::Approx(0.5f));
    CHECK_FALSE(material.baseColorTexture.has_value());
    CHECK(material.baseColorFactor == glm::vec4{1.0f}); // la valeur de glTF par défaut
    // Les deux images sont embarquées : désignées par {modèle, indice}, et gardées décodées.
    CHECK(material.metallicRoughnessTexture ==
          levain::assets::AssetRef{.asset = TestModelId, .sub = 0});
    CHECK(material.normalTexture == levain::assets::AssetRef{.asset = TestModelId, .sub = 1});
    CHECK(model->embeddedImages.size() == 2);
}
