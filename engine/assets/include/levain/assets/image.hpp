#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

#include "levain/core/error.hpp"

namespace levain::assets
{

/// Une image décodée, prête à envoyer au GPU : quatre octets par pixel (RGBA), lignes contiguës,
/// couleurs en sRGB comme dans le fichier.
struct Image
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;
};

/// Charge une image PNG, JPEG, TGA ou BMP (stb_image), convertie en RGBA quel que soit son format
/// d'origine. Un fichier absent ou illisible est un échec récupérable (ADR-0008).
[[nodiscard]] core::Result<Image> loadImage(const std::filesystem::path& path);

/// Décode une image déjà en mémoire, comme `loadImage` : une texture embarquée dans un glTF.
/// `name` ne sert qu'aux messages d'erreur.
[[nodiscard]] core::Result<Image> decodeImage(std::span<const std::byte> bytes,
                                              std::string_view name);

/// Une image HDR (M5.4) : de la lumière, pas des couleurs d'écran. Quatre `float` par pixel (RGBA,
/// alpha à 1), linéaires, sans limite à 1 : le soleil d'une HDRI vaut des milliers.
struct HdrImage
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<float> rgba;
};

/// Charge une image Radiance `.hdr` (stb_image), en lumière linéaire. Un fichier absent ou
/// illisible est un échec récupérable (ADR-0008).
[[nodiscard]] core::Result<HdrImage> loadHdrImage(const std::filesystem::path& path);

/// Écrit une image RGBA en PNG (stb_image_write). Sert aux captures d'écran du moteur, pour
/// montrer un rendu à qui n'a pas l'écran sous les yeux.
[[nodiscard]] core::Result<void> savePng(const std::filesystem::path& path, std::uint32_t width,
                                         std::uint32_t height, std::span<const std::uint8_t> rgba);

/// Nombre de niveaux de mip, de l'image elle-même jusqu'à 1 × 1, chaque niveau divisant la taille
/// par deux : 9 pour 256 × 256, comme pour 256 × 64.
[[nodiscard]] std::uint32_t mipCountFor(std::uint32_t width, std::uint32_t height);

/// Ce que portent les octets d'une image : des couleurs en sRGB, ou des données linéaires (une
/// normale, une rugosité), dont la moyenne se fait telle quelle.
enum class ImageEncoding : std::uint8_t
{
    Srgb,
    Linear,
};

/// La chaîne de mipmaps de `base` : le niveau 0 est `base`, le dernier fait 1 × 1. Calculée sur le
/// CPU, parce qu'elle le sera un jour à la cuisson des assets, sans GPU.
[[nodiscard]] std::vector<Image> buildMipChain(Image base,
                                               ImageEncoding encoding = ImageEncoding::Srgb);

} // namespace levain::assets
