#include "levain/assets/cooked_texture.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <memory>
#include <string>
#include <thread>

#include <ktx.h>

#include "levain/assets/cooked.hpp"
#include "levain/core/file.hpp"

namespace levain::assets
{

namespace
{

// Les formats Vulkan que KTX2 écrit dans son en-tête (vulkan_core.h, VkFormat). libktx les nomme
// ainsi ; les écrire en clair évite d'inclure Vulkan dans `assets`.
constexpr ktx_uint32_t VkFormatR8G8B8A8Srgb = 43;
constexpr ktx_uint32_t VkFormatBc7SrgbBlock = 146;

ktx_uint32_t vkFormatOf(TextureFormat format)
{
    return format == TextureFormat::Bc7Srgb ? VkFormatBc7SrgbBlock : VkFormatR8G8B8A8Srgb;
}

/// La clé des métadonnées KTX2 où le cuiseur écrit sa version et le hash de la source.
constexpr const char* SourceKey = "LevainSource";

std::string sourceStamp(std::uint64_t sourceHash)
{
    return std::format("cuiseur={} source={:016x}", CookerVersion, sourceHash);
}

/// Libère une texture libktx à la sortie de la portée, quoi qu'il arrive.
struct KtxDeleter
{
    void operator()(ktxTexture2* texture) const { ktxTexture_Destroy(ktxTexture(texture)); }
};

using KtxTexture = std::unique_ptr<ktxTexture2, KtxDeleter>;

std::unexpected<core::Error> ktxError(const std::filesystem::path& path, std::string_view step,
                                      KTX_error_code code)
{
    return core::makeError(core::ErrorCode::InvalidData,
                           std::format("{} : {} ({})", path.string(), step, ktxErrorString(code)));
}

} // namespace

TextureData textureDataOf(const std::vector<Image>& mips)
{
    TextureData data{.format = TextureFormat::Rgba8Srgb, .mips = {}};
    for (const Image& mip : mips)
    {
        const auto* bytes = reinterpret_cast<const std::byte*>(mip.rgba.data());
        data.mips.push_back({.width = mip.width,
                             .height = mip.height,
                             .bytes = std::vector<std::byte>(bytes, bytes + mip.rgba.size())});
    }
    return data;
}

core::Result<void> writeCookedTexture(const std::filesystem::path& path,
                                      const std::vector<Image>& mips, std::uint64_t sourceHash)
{
    if (mips.empty())
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               std::format("{} : aucune image à cuire", path.string()));
    }
    ktxTextureCreateInfo info{};
    info.vkFormat = VkFormatR8G8B8A8Srgb;
    info.baseWidth = mips.front().width;
    info.baseHeight = mips.front().height;
    info.baseDepth = 1;
    info.numDimensions = 2;
    info.numLevels = static_cast<ktx_uint32_t>(mips.size());
    info.numLayers = 1;
    info.numFaces = 1;
    info.isArray = KTX_FALSE;
    // Les mips viennent de buildMipChain, moyennées en lumière linéaire (M2.2) : libktx ne les
    // recalcule pas.
    info.generateMipmaps = KTX_FALSE;

    ktxTexture2* raw = nullptr;
    if (const auto code = ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &raw);
        code != KTX_SUCCESS)
    {
        return ktxError(path, "création", code);
    }
    const KtxTexture texture{raw};
    for (std::size_t level = 0; level < mips.size(); ++level)
    {
        const Image& mip = mips[level];
        if (const auto code = ktxTexture_SetImageFromMemory(ktxTexture(texture.get()),
                                                            static_cast<ktx_uint32_t>(level), 0, 0,
                                                            mip.rgba.data(), mip.rgba.size());
            code != KTX_SUCCESS)
        {
            return ktxError(path, "copie d'un niveau", code);
        }
    }

    ktxBasisParams params{};
    params.structSize = sizeof(params);
    params.uastc = KTX_TRUE;
    params.uastcFlags = KTX_PACK_UASTC_LEVEL_DEFAULT;
    params.threadCount = std::max(1u, std::thread::hardware_concurrency());
    if (const auto code = ktxTexture2_CompressBasisEx(texture.get(), &params); code != KTX_SUCCESS)
    {
        return ktxError(path, "encodage UASTC", code);
    }
    // La supercompression zstd s'ajoute à l'UASTC : elle réduit le fichier, et se défait au
    // chargement avant le transcodage. 18 : un niveau élevé, la cuisson a le temps.
    if (const auto code = ktxTexture2_DeflateZstd(texture.get(), 18); code != KTX_SUCCESS)
    {
        return ktxError(path, "supercompression zstd", code);
    }

    const std::string stamp = sourceStamp(sourceHash);
    ktxHashList_AddKVPair(&texture->kvDataHead, SourceKey,
                          static_cast<ktx_uint32_t>(stamp.size() + 1), stamp.c_str());

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (const auto code =
            ktxTexture_WriteToNamedFile(ktxTexture(texture.get()), core::pathForC(path).c_str());
        code != KTX_SUCCESS)
    {
        return ktxError(path, "écriture", code);
    }
    return {};
}

core::Result<void> writePlatformTexture(const std::filesystem::path& master,
                                        const std::filesystem::path& platform, TextureFormat target)
{
    ktxTexture2* raw = nullptr;
    if (const auto code = ktxTexture2_CreateFromNamedFile(
            core::pathForC(master).c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &raw);
        code != KTX_SUCCESS)
    {
        return ktxError(master, "lecture", code);
    }
    const KtxTexture texture{raw};
    const auto format = target == TextureFormat::Bc7Srgb ? KTX_TTF_BC7_RGBA : KTX_TTF_RGBA32;
    if (const auto code = ktxTexture2_TranscodeBasis(texture.get(), format, 0); code != KTX_SUCCESS)
    {
        return ktxError(master, "transcodage", code);
    }
    // Sans supercompression : le cache de plateforme se lit tel quel, sans rien décompresser. Les
    // métadonnées, dont le hash de la source, suivent le transcodage.
    if (const auto code = ktxTexture_WriteToNamedFile(ktxTexture(texture.get()),
                                                      core::pathForC(platform).c_str());
        code != KTX_SUCCESS)
    {
        return ktxError(platform, "écriture", code);
    }
    return {};
}

core::Result<TextureData> readCookedTexture(const std::filesystem::path& path,
                                            std::uint64_t sourceHash, TextureFormat target)
{
    ktxTexture2* raw = nullptr;
    if (const auto code = ktxTexture2_CreateFromNamedFile(
            core::pathForC(path).c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &raw);
        code != KTX_SUCCESS)
    {
        return ktxError(path, "lecture", code);
    }
    const KtxTexture texture{raw};

    unsigned int stampSize = 0;
    void* stampValue = nullptr;
    const std::string expected = sourceStamp(sourceHash);
    if (ktxHashList_FindValue(&texture->kvDataHead, SourceKey, &stampSize, &stampValue) !=
            KTX_SUCCESS ||
        std::string_view{static_cast<const char*>(stampValue), stampSize > 0 ? stampSize - 1 : 0} !=
            expected)
    {
        return core::makeError(
            core::ErrorCode::InvalidData,
            std::format("{} : périmé : la source ou le cuiseur ont changé", path.string()));
    }

    if (ktxTexture2_NeedsTranscoding(texture.get()))
    {
        const auto format = target == TextureFormat::Bc7Srgb ? KTX_TTF_BC7_RGBA : KTX_TTF_RGBA32;
        if (const auto code = ktxTexture2_TranscodeBasis(texture.get(), format, 0);
            code != KTX_SUCCESS)
        {
            return ktxError(path, "transcodage", code);
        }
    }
    else if (texture->vkFormat != vkFormatOf(target))
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               std::format("{} : cuit pour un autre format (VkFormat {})",
                                           path.string(), texture->vkFormat));
    }

    TextureData data{.format = target, .mips = {}};
    const ktx_uint8_t* bytes = ktxTexture_GetData(ktxTexture(texture.get()));
    for (ktx_uint32_t level = 0; level < texture->numLevels; ++level)
    {
        ktx_size_t offset = 0;
        ktxTexture_GetImageOffset(ktxTexture(texture.get()), level, 0, 0, &offset);
        const ktx_size_t size = ktxTexture_GetImageSize(ktxTexture(texture.get()), level);
        const auto* first = reinterpret_cast<const std::byte*>(bytes + offset);
        data.mips.push_back({.width = std::max(1u, texture->baseWidth >> level),
                             .height = std::max(1u, texture->baseHeight >> level),
                             .bytes = std::vector<std::byte>(first, first + size)});
    }
    return data;
}

} // namespace levain::assets
