#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include <doctest/doctest.h>

#include "levain/assets/asset_ref.hpp"
#include "levain/assets/cooked_texture.hpp"
#include "levain/assets/image.hpp"
#include "levain/assets/registry.hpp"

using levain::assets::readCookedTexture;
using levain::assets::TextureFormat;

namespace fs = std::filesystem;

namespace
{

constexpr std::uint64_t SourceHash = 0xabcd;

fs::path freshDirectory()
{
    const fs::path directory =
        fs::temp_directory_path() /
        ("levain-ktx-" + levain::assets::toString(levain::assets::generateAssetId()));
    fs::create_directories(directory);
    return directory;
}

/// Un dégradé de 8 × 8 : assez de variété pour que l'encodage ait quelque chose à perdre.
std::vector<levain::assets::Image> gradientMips()
{
    levain::assets::Image image{.width = 8, .height = 8, .rgba = {}};
    for (std::uint32_t y = 0; y < 8; ++y)
    {
        for (std::uint32_t x = 0; x < 8; ++x)
        {
            image.rgba.insert(image.rgba.end(), {static_cast<std::uint8_t>(x * 32),
                                                 static_cast<std::uint8_t>(y * 32), 128, 255});
        }
    }
    return levain::assets::buildMipChain(std::move(image));
}

} // namespace

TEST_CASE("une texture cuite en UASTC se relit, transcodée, avec tous ses niveaux")
{
    const fs::path directory = freshDirectory();
    const auto mips = gradientMips();
    REQUIRE(levain::assets::writeCookedTexture(directory / "t.ktx2", mips, SourceHash).has_value());

    const auto rgba = readCookedTexture(directory / "t.ktx2", SourceHash, TextureFormat::Rgba8Srgb);
    INFO("message d'erreur : " << (rgba ? std::string{} : rgba.error().message));
    REQUIRE(rgba.has_value());
    REQUIRE(rgba->mips.size() == 4); // 8, 4, 2, 1
    CHECK(rgba->mips[0].bytes.size() == 8 * 8 * 4);
    // L'UASTC perd peu : chaque canal du premier niveau reste à quelques unités de l'original.
    int worst = 0;
    for (std::size_t i = 0; i < mips[0].rgba.size(); ++i)
    {
        worst = std::max(worst, std::abs(static_cast<int>(mips[0].rgba[i]) -
                                         static_cast<int>(rgba->mips[0].bytes[i])));
    }
    CHECK(worst <= 16);

    const auto bc7 = readCookedTexture(directory / "t.ktx2", SourceHash, TextureFormat::Bc7Srgb);
    REQUIRE(bc7.has_value());
    CHECK(bc7->format == TextureFormat::Bc7Srgb);
    CHECK(bc7->mips[0].bytes.size() == 4 * 16); // 2 × 2 blocs de 16 octets
    CHECK(bc7->mips[3].bytes.size() == 16);     // 1 × 1 pixel : un bloc entier

    CHECK_FALSE(readCookedTexture(directory / "t.ktx2", SourceHash + 1, TextureFormat::Bc7Srgb)
                    .has_value()); // périmée
    fs::remove_all(directory);
}

TEST_CASE("le cache BC7 d'une plateforme se relit sans transcodage, et seulement en BC7")
{
    const fs::path directory = freshDirectory();
    REQUIRE(levain::assets::writeCookedTexture(directory / "t.ktx2", gradientMips(), SourceHash)
                .has_value());
    REQUIRE(levain::assets::writePlatformTexture(directory / "t.ktx2", directory / "t.bc7.ktx2",
                                                 TextureFormat::Bc7Srgb)
                .has_value());

    const auto bc7 =
        readCookedTexture(directory / "t.bc7.ktx2", SourceHash, TextureFormat::Bc7Srgb);
    REQUIRE(bc7.has_value());
    CHECK(bc7->mips.size() == 4);
    // Le hash de la source a suivi le transcodage : un cache périmé se voit aussi.
    CHECK_FALSE(readCookedTexture(directory / "t.bc7.ktx2", SourceHash + 1, TextureFormat::Bc7Srgb)
                    .has_value());
    // Déjà en BC7, il ne se transcode plus : demander du RGBA8 est un échec explicite.
    CHECK_FALSE(readCookedTexture(directory / "t.bc7.ktx2", SourceHash, TextureFormat::Rgba8Srgb)
                    .has_value());
    fs::remove_all(directory);
}

TEST_CASE("loadTextureData prend le cache de la plateforme, sinon la source")
{
    const fs::path root = freshDirectory();
    fs::copy_file(fs::path{LEVAIN_TEST_DATA_DIR} / "rgbw-2x2.png", root / "rgbw.png");
    levain::assets::AssetRegistry registry;
    REQUIRE(levain::assets::scanAssets(root, registry).has_value());
    const auto& [id, entry] = *registry.entries.begin();
    const levain::assets::AssetRef ref{.asset = id, .sub = 0};
    const levain::assets::ModelCache models;

    // Pas encore cuite : la source, en RGBA8, avec ses mips.
    const auto source =
        levain::assets::loadTextureData(registry, models, ref, TextureFormat::Bc7Srgb);
    REQUIRE(source.has_value());
    CHECK(source->format == TextureFormat::Rgba8Srgb);
    CHECK(source->mips.size() == 2);

    const auto stem = levain::assets::cookedTextureStem(registry, ref);
    REQUIRE(stem.has_value());
    fs::path master = stem.value_or(fs::path{});
    master += ".ktx2";
    fs::path platform = stem.value_or(fs::path{});
    platform += ".bc7.ktx2";
    const auto image = levain::assets::loadImage(entry.file);
    REQUIRE(image.has_value());
    REQUIRE(levain::assets::writeCookedTexture(master, levain::assets::buildMipChain(*image),
                                               entry.hash)
                .has_value());
    REQUIRE(
        levain::assets::writePlatformTexture(master, platform, TextureFormat::Bc7Srgb).has_value());

    const auto cooked =
        levain::assets::loadTextureData(registry, models, ref, TextureFormat::Bc7Srgb);
    REQUIRE(cooked.has_value());
    CHECK(cooked->format == TextureFormat::Bc7Srgb);

    // La même image lue comme des données n'a pas de version cuite : ce n'est pas le même
    // fichier, et ses mips viennent de la source, moyennées sur les octets. Le rouge, le vert, le
    // bleu et le blanc donnent 128 dans chaque canal, et non les 188 d'une moyenne en sRGB.
    const auto dataStem =
        levain::assets::cookedTextureStem(registry, ref, levain::assets::ImageEncoding::Linear);
    REQUIRE(dataStem.has_value());
    CHECK(dataStem.value_or(fs::path{}).filename().string().ends_with(".linear"));
    const auto data = levain::assets::loadTextureData(registry, models, ref, TextureFormat::Bc7Srgb,
                                                      levain::assets::ImageEncoding::Linear);
    REQUIRE(data.has_value());
    CHECK(data->format == TextureFormat::Rgba8Srgb);
    REQUIRE(data->mips.size() == 2);
    CHECK(std::to_integer<int>(data->mips[1].bytes[0]) == 128);
    CHECK(std::to_integer<int>(source->mips[1].bytes[0]) > 180);
    fs::remove_all(root);
}
