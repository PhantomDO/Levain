#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <doctest/doctest.h>

#include "levain/assets/asset_id.hpp"
#include "levain/assets/registry.hpp"

using levain::assets::AssetRegistry;
using levain::assets::metaPathOf;
using levain::assets::readMeta;
using levain::assets::scanAssets;

namespace fs = std::filesystem;

namespace
{

/// Un dossier temporaire propre à chaque test, vidé à la création et à la destruction. Son nom est
/// tiré au hasard : ctest peut lancer les tests en parallèle.
struct TempRoot
{
    fs::path path =
        fs::temp_directory_path() /
        ("levain-registry-" + levain::assets::toString(levain::assets::generateAssetId()));

    TempRoot()
    {
        fs::remove_all(path);
        fs::create_directories(path / "sous-dossier");
    }

    // Une exception qui sort d'un destructeur arrête tout le programme de test (std::terminate) :
    // l'effacement la rattrape et fait échouer le test en la nommant. Sous Windows, un fichier
    // resté ouvert, une poignée que le code testé aurait oubliée, l'empêche. La version à
    // `std::error_code` ne dirait pas où : son message ne donne que la cause (« Permission
    // denied »), quand `filesystem_error` nomme le dossier (sous Linux, jusqu'au fichier qui
    // résiste), ce qu'il faut sous Windows pour trouver la poignée oubliée.
    ~TempRoot()
    {
        try
        {
            fs::remove_all(path);
        }
        catch (const std::exception& failure)
        {
            // `doctest::String` : doctest écrirait un `const char*` comme un pointeur, et un
            // `std::string_view` passerait par les flux de la STL de Microsoft, dont le `throw;` de
            // `ios_base::clear` semble à clang-tidy relancer l'exception rattrapée.
            FAIL_CHECK("dossier temporaire non effacé : " << doctest::String{failure.what()});
        }
    }

    TempRoot(const TempRoot&) = delete;
    TempRoot& operator=(const TempRoot&) = delete;
};

void writeFile(const fs::path& path, std::string_view content)
{
    std::ofstream{path, std::ios::binary} << content;
}

levain::assets::AssetId idOf(const fs::path& asset)
{
    const auto meta = readMeta(metaPathOf(asset));
    REQUIRE(meta.has_value());
    return meta->id;
}

} // namespace

TEST_CASE("un GUID s'écrit en 32 chiffres hexadécimaux et se relit à l'identique")
{
    const auto id = levain::assets::generateAssetId();
    const std::string text = levain::assets::toString(id);
    CHECK(text.size() == 32);
    CHECK(levain::assets::parseAssetId(text) == id);
    CHECK_FALSE(levain::assets::parseAssetId("pas un guid").has_value());
    CHECK(levain::assets::generateAssetId() != id);
}

TEST_CASE("le scan donne un .meta aux nouveaux assets, et seulement à eux")
{
    TempRoot root;
    writeFile(root.path / "a.png", "image a");
    writeFile(root.path / "sous-dossier" / "b.gltf", "modèle b");
    writeFile(root.path / "b.bin", "données du glTF : pas un asset");

    AssetRegistry registry;
    const auto report = scanAssets(root.path, registry);
    REQUIRE(report.has_value());
    CHECK(report->created.size() == 2);
    CHECK(registry.entries.size() == 2);
    CHECK(fs::exists(root.path / "a.png.meta"));
    CHECK_FALSE(fs::exists(root.path / "b.bin.meta"));

    // Un second scan ne crée rien, et garde les mêmes GUID.
    const auto id = idOf(root.path / "a.png");
    AssetRegistry again;
    const auto second = scanAssets(root.path, again);
    REQUIRE(second.has_value());
    CHECK(second->created.empty());
    CHECK(levain::assets::pathOf(again, id) == root.path / "a.png");
}

TEST_CASE("un fichier renommé hors du moteur garde son GUID, rattaché par le hash")
{
    TempRoot root;
    writeFile(root.path / "foo.png", "une texture");
    AssetRegistry registry;
    REQUIRE(scanAssets(root.path, registry).has_value());
    const auto id = idOf(root.path / "foo.png");

    // Le gestionnaire de fichiers renomme et déplace le fichier, pas son .meta.
    fs::rename(root.path / "foo.png", root.path / "sous-dossier" / "bar.png");

    AssetRegistry after;
    const auto report = scanAssets(root.path, after);
    REQUIRE(report.has_value());
    CHECK(report->reattached.size() == 1);
    CHECK(report->orphans.empty());
    CHECK(idOf(root.path / "sous-dossier" / "bar.png") == id);
    CHECK_FALSE(fs::exists(root.path / "foo.png.meta"));
}

TEST_CASE("un fichier renommé et modifié à la fois se perd, et c'est signalé")
{
    TempRoot root;
    writeFile(root.path / "foo.png", "une texture");
    AssetRegistry registry;
    REQUIRE(scanAssets(root.path, registry).has_value());
    const auto id = idOf(root.path / "foo.png");

    fs::remove(root.path / "foo.png");
    writeFile(root.path / "bar.png", "une texture retouchée");

    AssetRegistry after;
    const auto report = scanAssets(root.path, after);
    REQUIRE(report.has_value());
    CHECK(report->created.size() == 1);
    REQUIRE(report->orphans.size() == 1);
    CHECK(report->orphans[0] == root.path / "foo.png.meta");
    CHECK(idOf(root.path / "bar.png") != id);
}

TEST_CASE("un fichier modifié sur place garde son GUID, et son hash suit")
{
    TempRoot root;
    writeFile(root.path / "foo.png", "avant");
    AssetRegistry registry;
    REQUIRE(scanAssets(root.path, registry).has_value());
    const auto before = readMeta(metaPathOf(root.path / "foo.png"));

    writeFile(root.path / "foo.png", "après");
    AssetRegistry after;
    REQUIRE(scanAssets(root.path, after).has_value());
    const auto updated = readMeta(metaPathOf(root.path / "foo.png"));

    REQUIRE(before.has_value());
    REQUIRE(updated.has_value());
    CHECK(updated->id == before->id);
    CHECK(updated->hash != before->hash);
}

TEST_CASE("deux fichiers de même GUID font échouer le scan, qui les nomme tous les deux")
{
    TempRoot root;
    writeFile(root.path / "foo.png", "une texture");
    AssetRegistry registry;
    REQUIRE(scanAssets(root.path, registry).has_value());

    // Une copie faite avec son .meta.
    fs::copy(root.path / "foo.png", root.path / "copie.png");
    fs::copy(root.path / "foo.png.meta", root.path / "copie.png.meta");

    AssetRegistry after;
    const auto report = scanAssets(root.path, after);
    REQUIRE_FALSE(report.has_value());
    const std::string& message = report.error().message;
    CHECK(message.find("copie.png") != std::string::npos);
    CHECK(message.find("foo.png") != std::string::npos);
}

namespace
{

/// Avance la date de modification d'une seconde : deux écritures rapprochées peuvent porter la
/// même date, que le noyau n'avance qu'à chaque tick de son horloge.
void touchLater(const fs::path& path)
{
    fs::last_write_time(path, fs::last_write_time(path) + std::chrono::seconds{1});
}

} // namespace

TEST_CASE("un asset modifié est rendu une fois, et son hash suit dans le registre et le .meta")
{
    TempRoot root;
    writeFile(root.path / "foo.png", "avant");
    writeFile(root.path / "bar.png", "intact");
    AssetRegistry registry;
    REQUIRE(scanAssets(root.path, registry).has_value());
    levain::assets::AssetWatch watch = levain::assets::watchAssets(registry);
    const levain::assets::AssetId id = idOf(root.path / "foo.png");
    const std::uint64_t before = registry.entries.at(id).hash;

    writeFile(root.path / "foo.png", "après");
    touchLater(root.path / "foo.png");
    const auto changed = levain::assets::takeChangedAssets(registry, watch);

    REQUIRE(changed.size() == 1);
    CHECK(changed.front() == id);
    CHECK(registry.entries.at(id).hash != before);
    CHECK(readMeta(metaPathOf(root.path / "foo.png"))->hash == registry.entries.at(id).hash);
    CHECK(levain::assets::takeChangedAssets(registry, watch).empty());
}

TEST_CASE("un asset réenregistré à l'identique n'est pas rendu")
{
    TempRoot root;
    writeFile(root.path / "foo.png", "même contenu");
    AssetRegistry registry;
    REQUIRE(scanAssets(root.path, registry).has_value());
    levain::assets::AssetWatch watch = levain::assets::watchAssets(registry);

    writeFile(root.path / "foo.png", "même contenu");
    touchLater(root.path / "foo.png");

    CHECK(levain::assets::takeChangedAssets(registry, watch).empty());
}
