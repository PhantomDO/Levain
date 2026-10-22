#include <filesystem>
#include <string>
#include <vector>

#include <doctest/doctest.h>
#include <flecs.h>

#include "levain/assets/asset_ref.hpp"
#include "levain/assets/gltf.hpp"
#include "levain/assets/registry.hpp"
#include "levain/scene/scene.hpp"

using levain::assets::AssetId;
using levain::assets::AssetsModule;
using levain::assets::MeshRef;
using levain::assets::referenceCount;
using levain::assets::takeUnusedAssets;

namespace fs = std::filesystem;

namespace
{

flecs::world assetWorld()
{
    flecs::world world;
    world.import<levain::scene::SceneModule>();
    world.import<AssetsModule>();
    return world;
}

MeshRef meshOf(AssetId asset)
{
    return MeshRef{.mesh = {.asset = asset, .sub = 0}};
}

} // namespace

// Chaque chemin par lequel une référence apparaît ou disparaît, un par un : c'est la garantie que
// les hooks du module n'en oublient aucun (ADR-0019).
TEST_CASE("le monde compte les références : pose, remplacement, retrait, destruction, copie")
{
    flecs::world world = assetWorld();
    const AssetId a = levain::assets::generateAssetId();
    const AssetId b = levain::assets::generateAssetId();

    const flecs::entity first = world.entity().set(meshOf(a));
    const flecs::entity second = world.entity().set(meshOf(a));
    CHECK(referenceCount(world, a) == 2);

    second.set(meshOf(b)); // remplacement : a perd une référence, b en gagne une
    CHECK(referenceCount(world, a) == 1);
    CHECK(referenceCount(world, b) == 1);

    second.set(meshOf(b)); // la même valeur : rien ne change
    CHECK(referenceCount(world, b) == 1);

    // Une copie se fait par `set` : `clone()` passe par `get_mut`, que flecs interdit sur un
    // composant qui a un hook on_replace (assertion de flecs, et non bug du comptage).
    // La valeur est copiée d'abord : `set(first.get<MeshRef>())` passerait une référence vers la
    // table de `first`, que l'ajout à une nouvelle entité de la même table peut réallouer (ASan).
    const MeshRef value = first.get<MeshRef>();
    const flecs::entity copy = world.entity().set(value);
    CHECK(referenceCount(world, a) == 2);

    first.remove<MeshRef>();
    CHECK(referenceCount(world, a) == 1);
    copy.destruct();
    second.destruct();
    CHECK(referenceCount(world, a) == 0);
    CHECK(referenceCount(world, b) == 0);
}

TEST_CASE("un asset tombé à zéro est à décharger, une seule fois, sauf s'il revient")
{
    flecs::world world = assetWorld();
    const AssetId a = levain::assets::generateAssetId();

    const flecs::entity entity = world.entity().set(meshOf(a));
    CHECK(takeUnusedAssets(world).empty());

    entity.destruct();
    const auto unused = takeUnusedAssets(world);
    REQUIRE(unused.size() == 1);
    CHECK(unused[0] == a);
    CHECK(takeUnusedAssets(world).empty()); // déjà rendu

    // Retiré puis repris dans la même image : il ne se décharge pas.
    const flecs::entity again = world.entity().set(meshOf(a));
    again.remove<MeshRef>();
    world.entity().set(meshOf(a));
    CHECK(takeUnusedAssets(world).empty());
}

namespace
{

/// La fin d'image du programme (app.cpp) : ce que plus aucune entité n'utilise se décharge.
void unloadUnused(flecs::world& world, levain::assets::ModelCache& cache)
{
    for (const AssetId& unused : takeUnusedAssets(world))
    {
        cache.models.erase(unused);
    }
}

} // namespace

TEST_CASE("pendant le jeu, un modèle dont la dernière entité meurt reste chargé jusqu'au Stop")
{
    flecs::world world = assetWorld();
    levain::assets::ModelCache cache;
    const AssetId fox = levain::assets::generateAssetId();
    cache.models.emplace(fox, levain::assets::Model{});
    const flecs::entity entity = world.entity().set(meshOf(fox));

    REQUIRE_FALSE(
        levain::assets::holdAssetsInUse(world.get_mut<levain::assets::AssetUsage>()).has_value());
    entity.destruct(); // ce que le jeu fait d'un renard
    unloadUnused(world, cache);
    unloadUnused(world, cache); // une image de plus
    CHECK(cache.models.contains(fox));

    // Stop : l'entité est réveillée avec son MeshRef, l'asset n'est plus à décharger.
    world.entity().set(meshOf(fox));
    levain::assets::releaseHeldAssets(world.get_mut<levain::assets::AssetUsage>());
    unloadUnused(world, cache);
    CHECK(cache.models.contains(fox));
}

TEST_CASE("l'asset retenu se décharge à la première image qui suit le Stop, s'il ne sert plus")
{
    flecs::world world = assetWorld();
    levain::assets::ModelCache cache;
    const AssetId tree = levain::assets::generateAssetId();
    cache.models.emplace(tree, levain::assets::Model{});
    // Né et mort pendant le jeu : le Stop ne le rend pas.
    REQUIRE_FALSE(
        levain::assets::holdAssetsInUse(world.get_mut<levain::assets::AssetUsage>()).has_value());
    world.entity().set(meshOf(tree)).destruct();
    unloadUnused(world, cache);
    CHECK(cache.models.contains(tree));

    levain::assets::releaseHeldAssets(world.get_mut<levain::assets::AssetUsage>());
    unloadUnused(world, cache);
    CHECK_FALSE(cache.models.contains(tree));
}

TEST_CASE("retenir deux fois les assets est refusé, et le Stop rend la main")
{
    flecs::world world = assetWorld();
    auto& usage = world.get_mut<levain::assets::AssetUsage>();
    REQUIRE_FALSE(levain::assets::holdAssetsInUse(usage).has_value());
    const std::string reason = levain::assets::holdAssetsInUse(usage).value_or("");
    CHECK(reason.find("holdAssetsInUse refusé") != std::string::npos);
    CHECK(usage.held); // le premier hold tient toujours

    levain::assets::releaseHeldAssets(usage);
    CHECK_FALSE(usage.held);
    CHECK_FALSE(levain::assets::holdAssetsInUse(usage).has_value()); // un Play suivant
}

TEST_CASE("un modèle se charge par son GUID, et ses entités le référencent")
{
    const fs::path root =
        fs::temp_directory_path() /
        ("levain-asset-ref-" + levain::assets::toString(levain::assets::generateAssetId()));
    fs::create_directories(root);
    fs::copy_file(fs::path{LEVAIN_TEST_DATA_DIR} / "two-nodes.gltf", root / "two-nodes.gltf");

    levain::assets::AssetRegistry registry;
    REQUIRE(levain::assets::scanAssets(root, registry).has_value());
    REQUIRE(registry.entries.size() == 1);
    const AssetId asset = registry.entries.begin()->first;

    levain::assets::ModelCache cache;
    const auto model = levain::assets::loadModel(cache, registry, asset);
    REQUIRE(model.has_value());
    CHECK(levain::assets::loadModel(cache, registry, asset).value_or(nullptr) == *model);

    flecs::world world = assetWorld();
    levain::assets::instantiateModel(world, **model, asset, "modele");
    CHECK(referenceCount(world, asset) == 2); // deux nœuds portent le triangle

    // Un GUID inconnu : l'asset a disparu, c'est un échec et pas un modèle vide.
    CHECK_FALSE(
        levain::assets::loadModel(cache, registry, levain::assets::generateAssetId()).has_value());
    fs::remove_all(root);
}

// Les épingles des commandes (ADR-0038, décision 4) : l'asset d'une entité supprimée reste chargé
// tant qu'une commande de la pile peut la réveiller.
TEST_CASE("un asset épinglé sans aucune entité n'est pas déchargé")
{
    flecs::world world = assetWorld();
    auto& usage = world.get_mut<levain::assets::AssetUsage>();
    const AssetId fox = levain::assets::generateAssetId();
    const flecs::entity entity = world.entity().set(meshOf(fox));
    const std::vector<AssetId> designated{fox};
    auto pin = levain::assets::pinAssets(usage, designated);

    entity.destruct(); // la commande Supprimer
    CHECK(takeUnusedAssets(world).empty());
    CHECK(takeUnusedAssets(world).empty()); // une image de plus
    levain::assets::releasePin(usage, pin);
}

TEST_CASE("deux commandes épinglent le même asset : en rendre une garde l'autre")
{
    flecs::world world = assetWorld();
    auto& usage = world.get_mut<levain::assets::AssetUsage>();
    const AssetId fox = levain::assets::generateAssetId();
    world.entity().set(meshOf(fox)).destruct();
    const std::vector<AssetId> designated{fox, fox}; // un doublon compte une fois
    auto first = levain::assets::pinAssets(usage, designated);
    CHECK(first.assets.size() == 1); // le doublon compte une fois
    auto second = levain::assets::pinAssets(usage, designated);

    levain::assets::releasePin(usage, first);
    CHECK(takeUnusedAssets(world).empty());

    levain::assets::releasePin(usage, second);
    CHECK(takeUnusedAssets(world).size() == 1);
}

TEST_CASE("rendre la dernière épingle d'un asset sans entité le décharge à l'image suivante")
{
    flecs::world world = assetWorld();
    levain::assets::ModelCache cache;
    auto& usage = world.get_mut<levain::assets::AssetUsage>();
    const AssetId fox = levain::assets::generateAssetId();
    cache.models.emplace(fox, levain::assets::Model{});
    const std::vector<AssetId> designated{fox};
    auto pin = levain::assets::pinAssets(usage, designated);
    world.entity().set(meshOf(fox)).destruct();
    unloadUnused(world, cache); // la fin d'image le laisse, et le sort de la liste
    CHECK(cache.models.contains(fox));

    levain::assets::releasePin(usage, pin);
    unloadUnused(world, cache);
    CHECK_FALSE(cache.models.contains(fox));
}

TEST_CASE("une épingle rendue pendant qu'une entité utilise l'asset ne le décharge pas")
{
    flecs::world world = assetWorld();
    auto& usage = world.get_mut<levain::assets::AssetUsage>();
    const AssetId fox = levain::assets::generateAssetId();
    world.entity().set(meshOf(fox));
    const std::vector<AssetId> designated{fox};
    auto pin = levain::assets::pinAssets(usage, designated);

    levain::assets::releasePin(usage, pin);
    CHECK(takeUnusedAssets(world).empty());
}

TEST_CASE(
    "épingles et retenue de Play sont indépendantes : l'asset sort quand les deux sont rendues")
{
    flecs::world world = assetWorld();
    auto& usage = world.get_mut<levain::assets::AssetUsage>();
    const AssetId fox = levain::assets::generateAssetId();
    const std::vector<AssetId> designated{fox};
    auto pin = levain::assets::pinAssets(usage, designated);
    REQUIRE_FALSE(levain::assets::holdAssetsInUse(usage).has_value());
    world.entity().set(meshOf(fox)).destruct();

    levain::assets::releasePin(usage, pin); // la pile se vide en jeu : Play retient encore
    CHECK(takeUnusedAssets(world).empty());
    levain::assets::releaseHeldAssets(usage);
    CHECK(takeUnusedAssets(world).size() == 1);
}

TEST_CASE("la retenue de Play rendue ne décharge pas un asset encore épinglé")
{
    flecs::world world = assetWorld();
    auto& usage = world.get_mut<levain::assets::AssetUsage>();
    const AssetId fox = levain::assets::generateAssetId();
    const std::vector<AssetId> designated{fox};
    auto pin = levain::assets::pinAssets(usage, designated);
    REQUIRE_FALSE(levain::assets::holdAssetsInUse(usage).has_value());
    world.entity().set(meshOf(fox)).destruct();

    levain::assets::releaseHeldAssets(usage);
    CHECK(takeUnusedAssets(world).empty());
    levain::assets::releasePin(usage, pin);
    CHECK(takeUnusedAssets(world).size() == 1);
}

TEST_CASE("l'asset tombé à zéro sous épingle revient, puis repart avec sa dernière entité")
{
    flecs::world world = assetWorld();
    auto& usage = world.get_mut<levain::assets::AssetUsage>();
    const AssetId fox = levain::assets::generateAssetId();
    const std::vector<AssetId> designated{fox};
    auto pin = levain::assets::pinAssets(usage, designated);
    world.entity().set(meshOf(fox)).destruct();
    CHECK(takeUnusedAssets(world).empty());

    const flecs::entity again = world.entity().set(meshOf(fox)); // l'annulation réveille l'entité
    levain::assets::releasePin(usage, pin);
    CHECK(takeUnusedAssets(world).empty()); // une entité l'utilise : rien à décharger

    again.destruct();
    CHECK(takeUnusedAssets(world).size() == 1);
}

TEST_CASE("un asset épinglé que rien ne compte sort quand sa dernière épingle est rendue")
{
    flecs::world world = assetWorld();
    auto& usage = world.get_mut<levain::assets::AssetUsage>();
    const AssetId collider = levain::assets::generateAssetId();
    // Un MeshCollider n'est pas compté : l'asset est dans la liste sans être jamais passé par zéro.
    usage.unused.insert(collider);
    const std::vector<AssetId> designated{collider};
    auto pin = levain::assets::pinAssets(usage, designated);
    CHECK(takeUnusedAssets(world).empty());

    levain::assets::releasePin(usage, pin);
    CHECK(takeUnusedAssets(world).size() == 1);
}

TEST_CASE("rendre une épingle deux fois ne vole pas l'épingle d'une autre commande")
{
    flecs::world world = assetWorld();
    auto& usage = world.get_mut<levain::assets::AssetUsage>();
    const AssetId fox = levain::assets::generateAssetId();
    world.entity().set(meshOf(fox)).destruct();
    const std::vector<AssetId> designated{fox};
    auto first = levain::assets::pinAssets(usage, designated);
    auto second = levain::assets::pinAssets(usage, designated);

    levain::assets::releasePin(usage, first);
    CHECK(first.assets.empty());
    levain::assets::releasePin(usage, first); // la seconde fois ne fait rien
    CHECK(takeUnusedAssets(world).empty());

    levain::assets::releasePin(usage, second);
    CHECK(takeUnusedAssets(world).size() == 1);
}

TEST_CASE("épingler rien n'épingle rien")
{
    flecs::world world = assetWorld();
    auto& usage = world.get_mut<levain::assets::AssetUsage>();
    const std::vector<AssetId> designated{AssetId{}};
    auto pin = levain::assets::pinAssets(usage, designated);
    CHECK(pin.assets.empty());
    CHECK(usage.pinned.empty());
}
