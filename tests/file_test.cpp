#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <vector>

#include <doctest/doctest.h>

#include "levain/core/file.hpp"

using levain::core::ErrorCode;
using levain::core::readFile;

TEST_CASE("readFile rend le contenu exact d'un fichier")
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "levain_file_test.bin";
    {
        std::ofstream file{path, std::ios::binary};
        file.write("\x00\x01\xFF", 3);
    }

    const auto bytes = readFile(path);
    std::filesystem::remove(path);

    REQUIRE(bytes.has_value());
    CHECK(bytes->size() == 3);
    CHECK(bytes->at(2) == std::byte{0xFF});
}

TEST_CASE("readFile signale un fichier absent comme FileNotFound")
{
    const auto bytes = readFile("/chemin/qui/n/existe/pas");

    REQUIRE_FALSE(bytes.has_value());
    CHECK(bytes.error().code == ErrorCode::FileNotFound);
}

TEST_CASE("takeChangedFiles rend les fichiers créés ou modifiés, une seule fois")
{
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "levain_file_watch_test";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directory(directory);
    const std::filesystem::path shader = directory / "mesh.slang";
    std::ofstream{shader} << "// v1";

    levain::core::FileWatch watch = levain::core::watchDirectory(directory, ".slang");
    CHECK(levain::core::takeChangedFiles(watch).empty()); // rien depuis le départ

    // Une date posée à la main plutôt qu'une réécriture : deux écritures dans la même milliseconde
    // pourraient garder la même date sur certains systèmes de fichiers.
    std::filesystem::last_write_time(shader, std::filesystem::last_write_time(shader) +
                                                 std::chrono::seconds{1});
    std::ofstream{directory / "triangle.slang"} << "// nouveau";
    std::ofstream{directory / "mesh.slang~"} << "// copie d'un éditeur, ignorée";

    std::vector<std::filesystem::path> changed = levain::core::takeChangedFiles(watch);
    std::ranges::sort(changed);
    CHECK(changed == std::vector<std::filesystem::path>{shader, directory / "triangle.slang"});
    CHECK(levain::core::takeChangedFiles(watch).empty()); // déjà rendus

    std::filesystem::remove_all(directory);
}

TEST_CASE("takeChangedFiles ne rend rien pour un dossier absent")
{
    levain::core::FileWatch watch =
        levain::core::watchDirectory("/chemin/qui/n/existe/pas", ".slang");
    CHECK(levain::core::takeChangedFiles(watch).empty());
}

TEST_CASE("pathForC garde un chemin ASCII et un chemin UTF-8 octet pour octet")
{
    CHECK(levain::core::pathForC(std::filesystem::path{"dossier/image.png"}) ==
          "dossier/image.png");
    // « été » en octets UTF-8 (C3 A9, t, C3 A9). Sous Windows, `string()` ne les rend qu'avec le
    // manifeste UTF-8 de l'ADR-0035 : sans lui, ce cas échoue, et c'est voulu (règle n°7).
    CHECK(levain::core::pathForC(std::filesystem::path{u8"dossier/été.png"}) ==
          "dossier/\xC3\xA9t\xC3\xA9.png");
}
