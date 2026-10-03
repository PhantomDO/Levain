#include "levain/terrain/layer_textures.hpp"

#include <format>
#include <optional>
#include <vector>

#include "levain/assets/image.hpp"
#include "levain/core/log.hpp"

namespace levain::terrain
{

namespace
{

/// Les mips des trois couches d'une sorte (« diff », « nor_gl », « rough »), toutes de même taille
/// ; vide si un fichier manque.
std::optional<std::array<std::vector<assets::Image>, 3>>
loadKind(const std::filesystem::path& directory, std::string_view kind)
{
    std::array<std::vector<assets::Image>, 3> layers;
    for (std::size_t layer = 0; layer < LayerTextureNames.size(); ++layer)
    {
        const std::filesystem::path path =
            directory / std::format("{}_{}_1k.jpg", LayerTextureNames[layer], kind);
        auto image = assets::loadImage(path);
        if (!image)
        {
            core::log("terrain", core::LogLevel::Warning,
                      "{} : couches en couleurs unies (tools/fetch-assets.sh)",
                      image.error().message);
            return std::nullopt;
        }
        layers[layer] = assets::buildMipChain(std::move(*image));
        if (layers[layer].front().width != layers[0].front().width ||
            layers[layer].front().height != layers[0].front().height)
        {
            core::log("terrain", core::LogLevel::Warning,
                      "{} : pas à la taille des autres couches, couleurs unies", path.string());
            return std::nullopt;
        }
    }
    return layers;
}

/// Un tableau de trois textures, chacune avec ses mips.
nvrhi::TextureHandle createLayerArray(nvrhi::IDevice& device, nvrhi::ICommandList& commandList,
                                      const std::array<std::vector<assets::Image>, 3>& layers,
                                      nvrhi::Format format, const char* name)
{
    const assets::Image& base = layers[0].front();
    nvrhi::TextureHandle texture =
        device.createTexture(nvrhi::TextureDesc()
                                 .setDimension(nvrhi::TextureDimension::Texture2DArray)
                                 .setWidth(base.width)
                                 .setHeight(base.height)
                                 .setArraySize(3)
                                 .setMipLevels(static_cast<std::uint32_t>(layers[0].size()))
                                 .setFormat(format)
                                 .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                 .setKeepInitialState(true)
                                 .setDebugName(name));
    for (std::uint32_t layer = 0; layer < 3; ++layer)
    {
        for (std::uint32_t mip = 0; mip < layers[layer].size(); ++mip)
        {
            const assets::Image& image = layers[layer][mip];
            commandList.writeTexture(texture, layer, mip, image.rgba.data(),
                                     std::size_t{image.width} * 4);
        }
    }
    return texture;
}

/// Un texel par couche : la couleur unie des couches sans texture.
std::array<std::vector<assets::Image>, 3> uniformLayers(std::array<glm::u8vec4, 3> colors)
{
    std::array<std::vector<assets::Image>, 3> layers;
    for (std::size_t layer = 0; layer < 3; ++layer)
    {
        const glm::u8vec4 c = colors[layer];
        layers[layer].push_back({.width = 1, .height = 1, .rgba = {c.r, c.g, c.b, c.a}});
    }
    return layers;
}

} // namespace

core::Result<LayerTextures> loadLayerTextures(nvrhi::IDevice& device,
                                              nvrhi::ICommandList& commandList,
                                              const std::filesystem::path& directory)
{
    auto albedo = loadKind(directory, "diff");
    auto normal = albedo ? loadKind(directory, "nor_gl") : std::nullopt;
    auto roughness = normal ? loadKind(directory, "rough") : std::nullopt;
    if (!roughness)
    {
        // Des albédos de sol réels en sRGB (herbe, terre, roche), une normale droite, une
        // rugosité de 0,9.
        albedo = uniformLayers({{{68, 85, 38, 255}, {97, 89, 79, 255}, {124, 121, 116, 255}}});
        normal =
            uniformLayers({{{128, 128, 255, 255}, {128, 128, 255, 255}, {128, 128, 255, 255}}});
        roughness =
            uniformLayers({{{230, 230, 230, 255}, {230, 230, 230, 255}, {230, 230, 230, 255}}});
    }
    LayerTextures textures{
        .albedo = createLayerArray(device, commandList, *albedo, nvrhi::Format::SRGBA8_UNORM,
                                   "terrain : couleurs"),
        .normal = createLayerArray(device, commandList, *normal, nvrhi::Format::RGBA8_UNORM,
                                   "terrain : normales"),
        .roughness = createLayerArray(device, commandList, *roughness, nvrhi::Format::RGBA8_UNORM,
                                      "terrain : rugosités"),
    };
    if (!textures.albedo || !textures.normal || !textures.roughness)
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               "textures des couches du terrain refusées par NVRHI");
    }
    return textures;
}

} // namespace levain::terrain
