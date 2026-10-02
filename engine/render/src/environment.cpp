#include "levain/render/environment.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <format>
#include <numbers>
#include <vector>

#include <glm/gtc/packing.hpp>

#include "shader.hpp"

#include "levain/render/texture.hpp"

namespace levain::render
{

namespace
{

/// `numthreads` des deux passes de shaders/environment.slang.
constexpr std::uint32_t TexelsPerGroupSide = 8;

/// Le plus grand flottant 16 bits. Au-delà, la conversion donnerait l'infini, qui empoisonnerait
/// toutes les moyennes des mips : un soleil trop fort est écrêté, pas perdu.
float clampToHalf(float value)
{
    return std::min(value, 65504.0f);
}

/// L'image en flottants 16 bits, le format de l'environnement.
std::vector<std::uint16_t> halfPixelsOf(std::span<const float> rgba)
{
    std::vector<std::uint16_t> pixels(rgba.size());
    std::ranges::transform(rgba, pixels.begin(),
                           [](float value) { return glm::packHalf1x16(clampToHalf(value)); });
    return pixels;
}

/// Une passe de calcul : un shader, et un layout qui lit une texture (`sourceSlot`) et écrit une
/// face de cubemap.
struct CubePass
{
    nvrhi::ShaderHandle shader;
    nvrhi::BindingLayoutHandle layout;
    nvrhi::ComputePipelineHandle pipeline;
};

core::Result<CubePass> createCubePass(nvrhi::IDevice& device, const char* entry,
                                      std::uint32_t sourceSlot)
{
    auto shader = loadShader(device, entry, nvrhi::ShaderType::Compute);
    if (!shader)
    {
        return std::unexpected(shader.error());
    }
    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::Compute;
    layoutDesc.bindings = {nvrhi::BindingLayoutItem::Texture_SRV(sourceSlot),
                           nvrhi::BindingLayoutItem::Sampler(0),
                           nvrhi::BindingLayoutItem::Texture_UAV(0)};
    CubePass pass{.shader = std::move(*shader),
                  .layout = device.createBindingLayout(layoutDesc),
                  .pipeline = nullptr};
    pass.pipeline = device.createComputePipeline(
        nvrhi::ComputePipelineDesc().setComputeShader(pass.shader).addBindingLayout(pass.layout));
    if (!pass.layout || !pass.pipeline)
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               std::format("passe {} refusée par NVRHI", entry));
    }
    return pass;
}

/// Écrit le niveau `mip` de `cube`, en lisant `source` à la place `sourceSlot` du layout.
void dispatchCubePass(nvrhi::IDevice& device, nvrhi::ICommandList& commandList,
                      const CubePass& pass, nvrhi::BindingSetItem source, nvrhi::ISampler& sampler,
                      nvrhi::ITexture& cube, std::uint32_t mip)
{
    // Une vue 2D en tableau de la cubemap, et non une vue cube : un shader n'écrit dans une
    // storage texture que par faces (NVRHI, BindingSetItem::Texture_UAV, paramètre dimension).
    const nvrhi::BindingSetHandle bindings = device.createBindingSet(
        nvrhi::BindingSetDesc()
            .addItem(source)
            .addItem(nvrhi::BindingSetItem::Sampler(0, &sampler))
            .addItem(nvrhi::BindingSetItem::Texture_UAV(0, &cube, EnvironmentFormat,
                                                        nvrhi::TextureSubresourceSet(mip, 1, 0, 6),
                                                        nvrhi::TextureDimension::Texture2DArray)),
        pass.layout);
    nvrhi::ComputeState state;
    state.pipeline = pass.pipeline;
    state.addBindingSet(bindings);
    commandList.setComputeState(state);
    const std::uint32_t size = std::max(cube.getDesc().width >> mip, 1u);
    const std::uint32_t groups = (size + TexelsPerGroupSide - 1) / TexelsPerGroupSide;
    commandList.dispatch(groups, groups, 6);
}

} // namespace

core::Result<Environment> createEnvironment(nvrhi::IDevice& device, const EnvironmentImage& image,
                                            std::uint32_t cubeSize)
{
    if (image.width == 0 || image.height == 0 ||
        image.rgba.size() != std::size_t{image.width} * image.height * 4)
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               std::format("environnement : {} flottants pour {} × {} pixels",
                                           image.rgba.size(), image.width, image.height));
    }
    if (!std::has_single_bit(cubeSize))
    {
        // Chaque mip doit faire exactement la moitié du précédent : la moyenne 2 × 2 en dépend.
        return core::makeError(
            core::ErrorCode::InvalidData,
            std::format("environnement : {} texels de côté, pas une puissance de deux", cubeSize));
    }
    auto fromEquirect = createCubePass(device, "environment.cubeFromEquirect", 0);
    auto downsample = createCubePass(device, "environment.downsample", 1);
    if (!fromEquirect || !downsample)
    {
        return std::unexpected(!fromEquirect ? fromEquirect.error() : downsample.error());
    }

    Environment environment;
    environment.cube =
        device.createTexture(nvrhi::TextureDesc()
                                 .setDimension(nvrhi::TextureDimension::TextureCube)
                                 .setWidth(cubeSize)
                                 .setHeight(cubeSize)
                                 .setArraySize(6)
                                 .setMipLevels(std::bit_width(cubeSize))
                                 .setFormat(EnvironmentFormat)
                                 .setIsUAV(true)
                                 .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                 .setKeepInitialState(true)
                                 .setDebugName("environnement"));
    // Répété en largeur (la longitude fait le tour), étiré en hauteur : le haut et le bas de
    // l'image sont les deux pôles, qui ne se touchent pas.
    const nvrhi::SamplerHandle sampler =
        device.createSampler(nvrhi::SamplerDesc()
                                 .setAllFilters(true)
                                 .setAddressU(nvrhi::SamplerAddressMode::Wrap)
                                 .setAddressV(nvrhi::SamplerAddressMode::Clamp)
                                 .setAddressW(nvrhi::SamplerAddressMode::Clamp));
    if (!environment.cube || !sampler)
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               "environnement : cubemap refusée par NVRHI");
    }

    const std::vector<std::uint16_t> pixels = halfPixelsOf(image.rgba);
    const nvrhi::CommandListHandle commandList = device.createCommandList();
    commandList->open();
    const std::array levels{TextureLevel{
        .width = image.width, .height = image.height, .bytes = std::as_bytes(std::span{pixels})}};
    const nvrhi::TextureHandle equirect =
        createTexture(device, *commandList, levels, "environnement : HDRI", EnvironmentFormat);
    dispatchCubePass(device, *commandList, *fromEquirect,
                     nvrhi::BindingSetItem::Texture_SRV(0, equirect), *sampler, *environment.cube,
                     0);
    for (std::uint32_t mip = 1; mip < environment.cube->getDesc().mipLevels; ++mip)
    {
        // Le niveau au-dessus, seul dans sa vue : le shader le lit au niveau 0. NVRHI place la
        // barrière entre son écriture et sa lecture, mip par mip.
        dispatchCubePass(
            device, *commandList, *downsample,
            nvrhi::BindingSetItem::Texture_SRV(1, environment.cube, EnvironmentFormat,
                                               nvrhi::TextureSubresourceSet(mip - 1, 1, 0, 6)),
            *sampler, *environment.cube, mip);
    }
    commandList->close();
    // Les textures et les passes locales peuvent disparaître après l'envoi : NVRHI et Dawn gardent
    // ce qu'une command list utilise jusqu'à la fin de son exécution.
    device.executeCommandList(commandList);
    return environment;
}

glm::vec3 cubeDirectionOf(std::uint32_t face, glm::vec2 uv)
{
    switch (face)
    {
    case 0:
        return glm::normalize(glm::vec3{1.0f, -uv.y, -uv.x});
    case 1:
        return glm::normalize(glm::vec3{-1.0f, -uv.y, uv.x});
    case 2:
        return glm::normalize(glm::vec3{uv.x, 1.0f, uv.y});
    case 3:
        return glm::normalize(glm::vec3{uv.x, -1.0f, -uv.y});
    case 4:
        return glm::normalize(glm::vec3{uv.x, -uv.y, 1.0f});
    default:
        return glm::normalize(glm::vec3{-uv.x, -uv.y, -1.0f});
    }
}

glm::vec3 equirectDirectionOf(glm::vec2 uv)
{
    const float longitude = (uv.x - 0.5f) * 2.0f * std::numbers::pi_v<float>;
    const float polar = uv.y * std::numbers::pi_v<float>;
    return {std::sin(polar) * std::cos(longitude), std::cos(polar),
            std::sin(polar) * std::sin(longitude)};
}

} // namespace levain::render
