#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include <doctest/doctest.h>

#include "levain/assets/image.hpp"

using levain::assets::buildMipChain;
using levain::assets::Image;
using levain::assets::loadImage;
using levain::assets::mipCountFor;
using levain::core::ErrorCode;

namespace
{

Image uniformImage(std::uint32_t width, std::uint32_t height, std::uint8_t value)
{
    return Image{.width = width,
                 .height = height,
                 .rgba = std::vector<std::uint8_t>(std::size_t{width} * height * 4, value)};
}

} // namespace

TEST_CASE("loadImage rend les pixels d'un PNG en RGBA, ligne par ligne")
{
    // tests/data/rgbw-2x2.png, créé par :
    //   magick -size 2x2 xc:red -fill lime -draw 'point 1,0' -fill blue -draw 'point 0,1' \
    //     -fill white -draw 'point 1,1' -strip PNG24:tests/data/rgbw-2x2.png
    // PNG24 : un fichier RGB sans alpha, que loadImage doit compléter d'un alpha opaque.
    const auto image = loadImage(std::filesystem::path{LEVAIN_TEST_DATA_DIR} / "rgbw-2x2.png");

    REQUIRE(image.has_value());
    CHECK(image->width == 2);
    CHECK(image->height == 2);
    const std::vector<std::uint8_t> expected{
        255, 0,   0,   255, // rouge
        0,   255, 0,   255, // vert
        0,   0,   255, 255, // bleu
        255, 255, 255, 255, // blanc
    };
    CHECK(image->rgba == expected);
}

TEST_CASE("savePng écrit une image que loadImage relit à l'identique")
{
    // Les quatre canaux distincts : un rouge et un bleu échangés, ou un alpha perdu, se verraient.
    const std::vector<std::uint8_t> pixels{255, 0, 0,   255, 0,  255, 0,  128,
                                           0,   0, 255, 255, 10, 20,  30, 0};
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "levain-save.png";

    REQUIRE(levain::assets::savePng(path, 2, 2, pixels).has_value());
    const auto reloaded = loadImage(path);
    std::filesystem::remove(path);

    REQUIRE(reloaded.has_value());
    CHECK(reloaded->width == 2);
    CHECK(reloaded->height == 2);
    CHECK(reloaded->rgba == pixels);
}

TEST_CASE("loadImage signale un fichier absent comme FileNotFound")
{
    const auto image = loadImage("/chemin/qui/n/existe/pas.png");

    REQUIRE_FALSE(image.has_value());
    CHECK(image.error().code == ErrorCode::FileNotFound);
}

TEST_CASE("loadImage signale un fichier qui n'est pas une image comme InvalidData")
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "levain_image_test.png";
    {
        std::ofstream file{path, std::ios::binary};
        file << "pas une image";
    }

    const auto image = loadImage(path);
    std::filesystem::remove(path);

    REQUIRE_FALSE(image.has_value());
    CHECK(image.error().code == ErrorCode::InvalidData);
}

TEST_CASE("mipCountFor compte les niveaux jusqu'à 1 × 1 sur le plus grand côté")
{
    CHECK(mipCountFor(1, 1) == 1);
    CHECK(mipCountFor(256, 256) == 9);
    CHECK(mipCountFor(256, 64) == 9);
    CHECK(mipCountFor(300, 200) == 9); // 300, 150, 75, 37, 18, 9, 4, 2, 1
}

TEST_CASE("buildMipChain divise chaque côté par deux, sans descendre sous 1")
{
    const std::vector<Image> levels = buildMipChain(uniformImage(4, 1, 200));

    REQUIRE(levels.size() == 3);
    CHECK(levels[1].width == 2);
    CHECK(levels[1].height == 1);
    CHECK(levels[2].width == 1);
    CHECK(levels[2].height == 1);
    CHECK(levels[2].rgba.size() == 4);
}

TEST_CASE("buildMipChain fait la moyenne en lumière linéaire, pas sur les octets sRGB")
{
    // Un damier noir et blanc réduit à un pixel : la moitié de la lumière du blanc, soit 188 en
    // sRGB. Une moyenne des octets donnerait 128, un gris nettement plus sombre que le damier vu de
    // loin.
    Image checker = uniformImage(2, 2, 255);
    for (const std::size_t black : {0U, 3U}) // pixels (0, 0) et (1, 1)
    {
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            checker.rgba[black * 4 + channel] = 0;
        }
    }

    const std::vector<Image> levels = buildMipChain(checker);

    REQUIRE(levels.size() == 2);
    for (std::size_t channel = 0; channel < 3; ++channel)
    {
        CHECK(levels[1].rgba[channel] >= 187);
        CHECK(levels[1].rgba[channel] <= 189);
    }
    CHECK(levels[1].rgba[3] == 255); // l'alpha est linéaire : il reste opaque
}

TEST_CASE("buildMipChain fait la moyenne des octets d'une image de données, comme une normale")
{
    // Des normales qui penchent l'une à gauche (64), l'autre à droite (192) : leur moyenne est
    // droite (128). En sRGB, elle pencherait à droite (≈ 150).
    Image normals = uniformImage(2, 1, 128);
    normals.rgba[0] = 64;
    normals.rgba[4] = 192;

    const std::vector<Image> levels = buildMipChain(normals, levain::assets::ImageEncoding::Linear);

    REQUIRE(levels.size() == 2);
    CHECK(levels[1].rgba[0] == 128);
}

TEST_CASE("loadHdrImage lit la lumière d'une HDR, au-delà de 1, et refuse une image 8 bits")
{
    // Deux pixels écrits à la main en RGBE : (1 ; 0,5 ; 0,25) puis (4 ; 4 ; 4), exacts en RGBE.
    const auto image = levain::assets::loadHdrImage(std::filesystem::path{LEVAIN_TEST_DATA_DIR} /
                                                    "two-lights.hdr");
    INFO("message d'erreur : " << (image ? std::string{} : image.error().message));
    REQUIRE(image.has_value());
    CHECK(image->width == 2);
    CHECK(image->height == 1);
    const std::vector<float> expected{1.0f, 0.5f, 0.25f, 1.0f, 4.0f, 4.0f, 4.0f, 1.0f};
    CHECK(image->rgba == expected);

    const auto notHdr =
        levain::assets::loadHdrImage(std::filesystem::path{LEVAIN_TEST_DATA_DIR} / "rgbw-2x2.png");
    CHECK_FALSE(notHdr.has_value());
}
