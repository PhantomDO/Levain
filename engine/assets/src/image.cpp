#include "levain/assets/image.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cmath>
#include <cstddef>
#include <format>
#include <memory>
#include <utility>

// Le code de stb_image et de stb_image_write est compilé ici : leurs macros sont posées sur ce
// fichier par engine/assets/CMakeLists.txt.
#include <stb_image.h>
#include <stb_image_write.h>

#include "levain/core/assert.hpp"
#include "levain/core/file.hpp"

namespace levain::assets
{

namespace
{

constexpr std::size_t BytesPerPixel = 4;

std::size_t rgbaSize(std::uint32_t width, std::uint32_t height)
{
    return std::size_t{width} * height * BytesPerPixel;
}

/// Octet sRGB vers lumière linéaire, formule de la norme sRGB (IEC 61966-2-1). Une table de 256
/// valeurs : la conversion revient quatre fois par canal et par pixel réduit.
float srgbToLinear(std::uint8_t value)
{
    static const std::array<float, 256> table = []
    {
        std::array<float, 256> result{};
        for (std::size_t i = 0; i < result.size(); ++i)
        {
            const float c = static_cast<float>(i) / 255.0f;
            result[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        return result;
    }();
    return table[value];
}

std::uint8_t linearToSrgb(float linear)
{
    const float c =
        linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    return static_cast<std::uint8_t>(std::lround(std::clamp(c, 0.0f, 1.0f) * 255.0f));
}

/// Décalage du texel (x, y) dans `image.rgba`, ramené dans l'image : sur un côté de 1, le texel
/// voisin est le texel lui-même.
std::size_t texelOffset(const Image& image, std::uint32_t x, std::uint32_t y)
{
    const std::size_t column = std::min(x, image.width - 1);
    const std::size_t row = std::min(y, image.height - 1);
    return (row * image.width + column) * BytesPerPixel;
}

/// Le niveau de mip suivant : chaque texel est la moyenne des 2 × 2 texels qu'il recouvre (filtre
/// boîte, le filtre classique des mipmaps).
///
/// Le piège : les octets d'une image sRGB ne sont pas proportionnels à la lumière. En faire la
/// moyenne directement assombrit chaque niveau (un damier noir et blanc donnerait 128 au lieu de
/// 188). On convertit donc en linéaire, on fait la moyenne, puis on reconvertit. L'alpha, lui, est
/// déjà linéaire. Une image de données (`ImageEncoding::Linear`) se moyenne telle quelle.
///
/// ponytail: un côté impair perd sa dernière rangée (3 → 1) et les couleurs ne sont pas pondérées
/// par l'alpha. Suffisant pour des textures opaques en puissances de deux ; un filtre à trois
/// texels et la pondération par l'alpha le jour où une texture ne l'est pas.
Image downsampleInLinearSpace(const Image& source, ImageEncoding encoding)
{
    const std::uint32_t width = std::max(source.width / 2, 1U);
    const std::uint32_t height = std::max(source.height / 2, 1U);
    Image result{.width = width, .height = height, .rgba = {}};
    result.rgba.resize(rgbaSize(width, height));

    for (std::uint32_t y = 0; y < height; ++y)
    {
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const std::array<std::size_t, 4> covered{
                texelOffset(source, 2 * x, 2 * y), texelOffset(source, 2 * x + 1, 2 * y),
                texelOffset(source, 2 * x, 2 * y + 1), texelOffset(source, 2 * x + 1, 2 * y + 1)};
            const std::size_t out = (std::size_t{y} * width + x) * BytesPerPixel;
            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                if (encoding == ImageEncoding::Linear)
                {
                    unsigned sum = 0;
                    for (const std::size_t texel : covered)
                    {
                        sum += source.rgba[texel + channel];
                    }
                    result.rgba[out + channel] = static_cast<std::uint8_t>((sum + 2) / 4);
                    continue;
                }
                float light = 0.0f;
                for (const std::size_t texel : covered)
                {
                    light += srgbToLinear(source.rgba[texel + channel]);
                }
                result.rgba[out + channel] = linearToSrgb(light / 4.0f);
            }
            unsigned alpha = 0;
            for (const std::size_t texel : covered)
            {
                alpha += source.rgba[texel + 3];
            }
            result.rgba[out + 3] = static_cast<std::uint8_t>((alpha + 2) / 4); // arrondi
        }
    }
    return result;
}

} // namespace

core::Result<Image> decodeImage(std::span<const std::byte> bytes, std::string_view name)
{
    if (bytes.size() > INT_MAX)
    {
        return core::makeError(core::ErrorCode::Unsupported,
                               std::format("{} : plus de 2 Go, trop gros pour stb_image", name));
    }

    int width = 0;
    int height = 0;
    int channelsInFile = 0;
    // STBI_rgb_alpha : une image en niveaux de gris ou sans alpha arrive quand même en RGBA, le
    // seul format que le GPU recevra.
    const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels{
        stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
                              static_cast<int>(bytes.size()), &width, &height, &channelsInFile,
                              STBI_rgb_alpha),
        &stbi_image_free};
    if (!pixels)
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               std::format("{} : {}", name, stbi_failure_reason()));
    }

    Image image{.width = static_cast<std::uint32_t>(width),
                .height = static_cast<std::uint32_t>(height),
                .rgba = {}};
    image.rgba.assign(pixels.get(), pixels.get() + rgbaSize(image.width, image.height));
    return image;
}

core::Result<Image> loadImage(const std::filesystem::path& path)
{
    auto bytes = core::readFile(path);
    if (!bytes)
    {
        return std::unexpected(bytes.error());
    }
    return decodeImage(*bytes, path.string());
}

core::Result<HdrImage> loadHdrImage(const std::filesystem::path& path)
{
    auto bytes = core::readFile(path);
    if (!bytes)
    {
        return std::unexpected(bytes.error());
    }
    if (bytes->size() > INT_MAX)
    {
        return core::makeError(
            core::ErrorCode::Unsupported,
            std::format("{} : plus de 2 Go, trop gros pour stb_image", path.string()));
    }
    const auto* data = reinterpret_cast<const stbi_uc*>(bytes->data());
    const int size = static_cast<int>(bytes->size());
    // Une image en 8 bits passerait par stbi_loadf convertie depuis le sRGB : on la refuse, une
    // HDRI doit garder sa vraie lumière.
    if (stbi_is_hdr_from_memory(data, size) == 0)
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               std::format("{} : pas une image HDR", path.string()));
    }
    int width = 0;
    int height = 0;
    int channelsInFile = 0;
    const std::unique_ptr<float, decltype(&stbi_image_free)> pixels{
        stbi_loadf_from_memory(data, size, &width, &height, &channelsInFile, STBI_rgb_alpha),
        &stbi_image_free};
    if (!pixels)
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               std::format("{} : {}", path.string(), stbi_failure_reason()));
    }
    HdrImage image{.width = static_cast<std::uint32_t>(width),
                   .height = static_cast<std::uint32_t>(height),
                   .rgba = {}};
    image.rgba.assign(pixels.get(), pixels.get() + rgbaSize(image.width, image.height));
    return image;
}

core::Result<void> savePng(const std::filesystem::path& path, std::uint32_t width,
                           std::uint32_t height, std::span<const std::uint8_t> rgba)
{
    LEVAIN_ASSERT(rgba.size() == rgbaSize(width, height), "taille des pixels incohérente");
    const int rowBytes = static_cast<int>(width) * 4;
    if (stbi_write_png(core::pathForC(path).c_str(), static_cast<int>(width),
                       static_cast<int>(height), 4, rgba.data(), rowBytes) == 0)
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               std::format("{} : écriture du PNG impossible", path.string()));
    }
    return {};
}

std::uint32_t mipCountFor(std::uint32_t width, std::uint32_t height)
{
    LEVAIN_ASSERT(width > 0 && height > 0, "une image vide n'a pas de mipmaps");
    return static_cast<std::uint32_t>(std::bit_width(std::max(width, height)));
}

std::vector<Image> buildMipChain(Image base, ImageEncoding encoding)
{
    std::vector<Image> levels;
    levels.reserve(mipCountFor(base.width, base.height));
    levels.push_back(std::move(base));
    while (levels.back().width > 1 || levels.back().height > 1)
    {
        // Un côté déjà à 1 le reste : une texture 4 × 1 donne 2 × 1, puis 1 × 1.
        Image next = downsampleInLinearSpace(levels.back(), encoding);
        levels.push_back(std::move(next));
    }
    return levels;
}

} // namespace levain::assets
