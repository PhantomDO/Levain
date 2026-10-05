#include <cstddef>
#include <filesystem>
#include <fstream>
#include <vector>

#include <doctest/doctest.h>

#include "levain/assets/asset_ref.hpp"
#include "levain/assets/cooked.hpp"
#include "levain/assets/gltf.hpp"
#include "levain/assets/registry.hpp"
#include "levain/core/file.hpp"

using levain::assets::readCookedModel;
using levain::assets::writeCookedModel;

namespace fs = std::filesystem;

namespace
{

constexpr std::uint64_t SourceHash = 0x1234;

fs::path freshDirectory()
{
    const fs::path directory =
        fs::temp_directory_path() /
        ("levain-cooked-" + levain::assets::toString(levain::assets::generateAssetId()));
    fs::create_directories(directory);
    return directory;
}

/// Le modèle de test, avec son image embarquée : tout ce qu'un `.lvmesh` doit transporter.
levain::assets::Model testModel()
{
    auto model = levain::assets::loadGltf(fs::path{LEVAIN_TEST_DATA_DIR} / "two-nodes.gltf",
                                          levain::assets::AssetId{.high = 1, .low = 2},
                                          levain::assets::AssetRegistry{});
    REQUIRE(model.has_value());
    return std::move(*model);
}

} // namespace

TEST_CASE("un .lvmesh relu rend le même modèle")
{
    const fs::path directory = freshDirectory();
    levain::assets::Model model = testModel();
    // Un nom vide : un tableau de taille nulle, que le lecteur doit copier sans memcpy (UBSan).
    model.nodes[0].name.clear();
    REQUIRE(writeCookedModel(directory / "m.lvmesh", model, SourceHash).has_value());

    const auto read = readCookedModel(directory / "m.lvmesh", SourceHash);
    INFO("message d'erreur : " << (read ? std::string{} : read.error().message));
    REQUIRE(read.has_value());
    REQUIRE(read->meshes.size() == model.meshes.size());
    const auto& primitive = read->meshes[0].primitives[0];
    CHECK(primitive.indices == model.meshes[0].primitives[0].indices);
    CHECK(primitive.vertices[1].position == model.meshes[0].primitives[0].vertices[1].position);
    CHECK(primitive.material == model.meshes[0].primitives[0].material);
    REQUIRE(read->nodes.size() == 2);
    CHECK(read->nodes[0].name.empty());
    CHECK(read->nodes[1].parent == 0u);
    CHECK(read->nodes[1].local.scale == model.nodes[1].local.scale);
    CHECK(read->materials[0].baseColorTexture == model.materials[0].baseColorTexture);
    CHECK(read->embeddedImages.at(0).rgba == model.embeddedImages.at(0).rgba);
    fs::remove_all(directory);
}

TEST_CASE("un .lvmesh garde les matériaux metallic-roughness et les tangentes")
{
    const fs::path directory = freshDirectory();
    auto model = levain::assets::loadGltf(fs::path{LEVAIN_TEST_DATA_DIR} / "materials.gltf",
                                          levain::assets::AssetId{.high = 1, .low = 2},
                                          levain::assets::AssetRegistry{});
    REQUIRE(model.has_value());
    model->meshes[0].primitives[0].vertices[2].tangent = {0.0f, 0.0f, -1.0f, -1.0f};
    // materials.gltf est en « MASK » : l'aller-retour vérifie l'autre valeur.
    REQUIRE(model->materials[0].alphaMasked);
    model->materials[0].alphaMasked = false;
    REQUIRE(writeCookedModel(directory / "m.lvmesh", *model, SourceHash).has_value());

    const auto read = readCookedModel(directory / "m.lvmesh", SourceHash);
    INFO("message d'erreur : " << (read ? std::string{} : read.error().message));
    REQUIRE(read.has_value());
    const levain::assets::ModelMaterial& expected = model->materials[0];
    const levain::assets::ModelMaterial& material = read->materials[0];
    CHECK(material.metallicFactor == expected.metallicFactor);
    CHECK(material.roughnessFactor == expected.roughnessFactor);
    CHECK(material.metallicRoughnessTexture == expected.metallicRoughnessTexture);
    CHECK(material.normalTexture == expected.normalTexture);
    CHECK(material.normalScale == expected.normalScale);
    CHECK_FALSE(material.alphaMasked);
    CHECK(read->meshes[0].primitives[0].vertices[2].tangent == glm::vec4{0.0f, 0.0f, -1.0f, -1.0f});
    fs::remove_all(directory);
}

TEST_CASE("un .lvmesh garde le skinning des sommets et les nœuds os")
{
    const fs::path directory = freshDirectory();
    auto model = levain::assets::loadGltf(fs::path{LEVAIN_TEST_DATA_DIR} / "two-joints.gltf",
                                          levain::assets::AssetId{.high = 1, .low = 2},
                                          levain::assets::AssetRegistry{});
    REQUIRE(model.has_value());
    REQUIRE(writeCookedModel(directory / "m.lvmesh", *model, SourceHash).has_value());

    const auto read = readCookedModel(directory / "m.lvmesh", SourceHash);
    REQUIRE(read.has_value());
    CHECK(read->meshes[0].primitives[0].joints == model->meshes[0].primitives[0].joints);
    CHECK(read->meshes[0].primitives[0].weights == model->meshes[0].primitives[0].weights);
    REQUIRE(read->nodes.size() == model->nodes.size());
    for (std::size_t n = 0; n < read->nodes.size(); ++n)
    {
        CHECK(read->nodes[n].joint == model->nodes[n].joint);
    }
    fs::remove_all(directory);
}

TEST_CASE("un .lvmesh périmé, tronqué ou d'un encodage inconnu est refusé")
{
    const fs::path directory = freshDirectory();
    const fs::path path = directory / "m.lvmesh";
    REQUIRE(writeCookedModel(path, testModel(), SourceHash).has_value());

    // La source a changé depuis la cuisson.
    CHECK_FALSE(readCookedModel(path, SourceHash + 1).has_value());

    auto bytes = levain::core::readFile(path);
    REQUIRE(bytes.has_value());
    std::vector<std::byte> original = *bytes;

    // Tronqué : le lecteur vérifie chaque borne.
    std::ofstream{path, std::ios::binary}.write(reinterpret_cast<const char*>(original.data()),
                                                static_cast<std::streamsize>(original.size() / 2));
    CHECK_FALSE(readCookedModel(path, SourceHash).has_value());

    // L'encodage suit la signature et la version (4 + 4 octets) : 1 est réservé, donc refusé.
    original[8] = std::byte{1};
    // Le format 4, d'avant la transparence découpée des matériaux (M6.3) : refusé, donc recuit.
    std::vector<std::byte> previousFormat = original;
    previousFormat[8] = std::byte{0};
    previousFormat[4] = std::byte{4};
    std::ofstream{path, std::ios::binary}.write(
        reinterpret_cast<const char*>(previousFormat.data()),
        static_cast<std::streamsize>(previousFormat.size()));
    CHECK_FALSE(readCookedModel(path, SourceHash).has_value());
    std::ofstream{path, std::ios::binary}.write(reinterpret_cast<const char*>(original.data()),
                                                static_cast<std::streamsize>(original.size()));
    const auto reserved = readCookedModel(path, SourceHash);
    REQUIRE_FALSE(reserved.has_value());
    CHECK(reserved.error().message.find("encodage") != std::string::npos);
    fs::remove_all(directory);
}

TEST_CASE("loadModel prend la version cuite quand elle est à jour")
{
    const fs::path root = freshDirectory();
    fs::copy_file(fs::path{LEVAIN_TEST_DATA_DIR} / "two-nodes.gltf", root / "two-nodes.gltf");
    levain::assets::AssetRegistry registry;
    REQUIRE(levain::assets::scanAssets(root, registry).has_value());
    const auto& [id, entry] = *registry.entries.begin();

    const auto source = levain::assets::loadGltf(entry.file, id, registry);
    REQUIRE(source.has_value());
    const auto cooked = levain::assets::cookedPathOf(registry, id, ".lvmesh");
    REQUIRE(cooked.has_value());
    REQUIRE(writeCookedModel(cooked.value_or(fs::path{}), *source, entry.hash).has_value());

    // La source effacée : seul le fichier cuit peut encore donner le modèle.
    fs::remove(entry.file);
    levain::assets::ModelCache cache;
    const auto model = levain::assets::loadModel(cache, registry, id);
    INFO("message d'erreur : " << (model ? std::string{} : model.error().message));
    CHECK(model.has_value());
    fs::remove_all(root);
}

TEST_CASE("un .lvcol relu rend la même collision, et périme avec sa source ou sa tolérance")
{
    const fs::path directory = freshDirectory();
    const fs::path path = directory / "m.lvcol";
    const levain::assets::CollisionMesh mesh{
        .vertices = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
        .indices = {0, 2, 1}};
    REQUIRE(levain::assets::writeCookedCollision(path, mesh, SourceHash, 0.02f).has_value());

    const auto read = levain::assets::readCookedCollision(path, SourceHash, 0.02f);
    INFO("message d'erreur : " << (read ? std::string{} : read.error().message));
    REQUIRE(read.has_value());
    CHECK(read->vertices == mesh.vertices);
    CHECK(read->indices == mesh.indices);

    CHECK_FALSE(levain::assets::readCookedCollision(path, SourceHash + 1, 0.02f).has_value());
    // Simplifiée à une autre tolérance, ce n'est plus la collision demandée.
    CHECK_FALSE(levain::assets::readCookedCollision(path, SourceHash, 0.05f).has_value());

    // Un indice au-delà des sommets : Jolt ne le vérifierait qu'en Debug.
    const levain::assets::CollisionMesh broken{.vertices = mesh.vertices, .indices = {0, 2, 7}};
    REQUIRE(levain::assets::writeCookedCollision(path, broken, SourceHash, 0.02f).has_value());
    const auto refused = levain::assets::readCookedCollision(path, SourceHash, 0.02f);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().message.find("indices") != std::string::npos);
    fs::remove_all(directory);
}
