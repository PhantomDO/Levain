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

#include "levain/core/assert.hpp"
#include "levain/render/shader.hpp"
#include "levain/render/texture.hpp"

namespace levain::render
{

namespace
{

/// `numthreads` des passes de shaders/environment.slang.
constexpr std::uint32_t TexelsPerGroupSide = 8;

/// La luminance d'une couleur linéaire (Rec. 709) : ce que l'œil en perçoit.
float luminanceOf(glm::vec3 color)
{
    return glm::dot(color, glm::vec3{0.2126f, 0.7152f, 0.0722f});
}

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

/// Une passe de calcul : un shader, son layout et son pipeline.
struct ComputePass
{
    nvrhi::ShaderHandle shader;
    nvrhi::BindingLayoutHandle layout;
    nvrhi::ComputePipelineHandle pipeline;
};

core::Result<ComputePass> createComputePass(nvrhi::IDevice& device, const char* entry,
                                            std::vector<nvrhi::BindingLayoutItem> items)
{
    auto shader = loadShader(device, entry, nvrhi::ShaderType::Compute);
    if (!shader)
    {
        return std::unexpected(shader.error());
    }
    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::Compute;
    layoutDesc.bindings = std::move(items);
    ComputePass pass{.shader = std::move(*shader),
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

/// Une passe qui lit une texture à la place `sourceSlot` et écrit une face de cubemap.
core::Result<ComputePass> createCubePass(nvrhi::IDevice& device, const char* entry,
                                         std::uint32_t sourceSlot)
{
    return createComputePass(device, entry,
                             {nvrhi::BindingLayoutItem::Texture_SRV(sourceSlot),
                              nvrhi::BindingLayoutItem::Sampler(0),
                              nvrhi::BindingLayoutItem::Texture_UAV(0)});
}

/// Une cubemap que les passes écrivent, puis que les shaders lisent.
nvrhi::TextureHandle createCube(nvrhi::IDevice& device, std::uint32_t size, std::uint32_t mips,
                                const char* name)
{
    return device.createTexture(nvrhi::TextureDesc()
                                    .setDimension(nvrhi::TextureDimension::TextureCube)
                                    .setWidth(size)
                                    .setHeight(size)
                                    .setArraySize(6)
                                    .setMipLevels(mips)
                                    .setFormat(EnvironmentFormat)
                                    .setIsUAV(true)
                                    .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                    .setKeepInitialState(true)
                                    .setDebugName(name));
}

/// Écrit le niveau `mip` de `cube`, en lisant `source` à la place `sourceSlot` du layout.
void dispatchCubePass(nvrhi::IDevice& device, nvrhi::ICommandList& commandList,
                      const ComputePass& pass, nvrhi::BindingSetItem source,
                      nvrhi::ISampler& sampler, nvrhi::ITexture& cube, std::uint32_t mip)
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
    auto irradiance = createCubePass(device, "environment.irradiance", 2);
    auto prefilter = createCubePass(device, "environment.prefilterSpecular", 2);
    auto brdf = createComputePass(device, "environment.integrateBrdf",
                                  {nvrhi::BindingLayoutItem::Texture_UAV(1)});
    for (const auto* pass : {&fromEquirect, &downsample, &irradiance, &prefilter, &brdf})
    {
        if (!*pass)
        {
            return std::unexpected(pass->error());
        }
    }

    Environment environment{
        .cube = createCube(device, cubeSize, std::bit_width(cubeSize), "environnement"),
        .irradiance = createCube(device, IrradianceSize, 1, "environnement : irradiance"),
        .specular = createCube(device, SpecularSize, SpecularMips, "environnement : spéculaire"),
        .brdfLut = device.createTexture(nvrhi::TextureDesc()
                                            .setWidth(BrdfLutSize)
                                            .setHeight(BrdfLutSize)
                                            .setFormat(EnvironmentFormat)
                                            .setIsUAV(true)
                                            .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                            .setKeepInitialState(true)
                                            .setDebugName("environnement : BRDF")),
    };
    // Répété en largeur (la longitude fait le tour), étiré en hauteur : le haut et le bas de
    // l'image sont les deux pôles, qui ne se touchent pas. Une cubemap ignore ces modes.
    const nvrhi::SamplerHandle sampler =
        device.createSampler(nvrhi::SamplerDesc()
                                 .setAllFilters(true)
                                 .setAddressU(nvrhi::SamplerAddressMode::Wrap)
                                 .setAddressV(nvrhi::SamplerAddressMode::Clamp)
                                 .setAddressW(nvrhi::SamplerAddressMode::Clamp));
    if (!environment.cube || !environment.irradiance || !environment.specular ||
        !environment.brdfLut || !sampler)
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               "environnement : textures refusées par NVRHI");
    }

    const std::vector<std::uint16_t> pixels = halfPixelsOf(image.rgba);
    // Sans exécution immédiate : l'appelant peut avoir sa propre command list ouverte, et NVRHI
    // n'en admet qu'une immédiate ouverte à la fois (nvrhi.h, CommandListParameters).
    const nvrhi::CommandListHandle commandList =
        device.createCommandList(nvrhi::CommandListParameters().setEnableImmediateExecution(false));
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
    const nvrhi::BindingSetItem wholeCube = nvrhi::BindingSetItem::Texture_SRV(2, environment.cube);
    dispatchCubePass(device, *commandList, *irradiance, wholeCube, *sampler,
                     *environment.irradiance, 0);
    for (std::uint32_t mip = 0; mip < SpecularMips; ++mip)
    {
        dispatchCubePass(device, *commandList, *prefilter, wholeCube, *sampler,
                         *environment.specular, mip);
    }
    // Le handle tient le binding set en vie : ComputeState n'en garde qu'un pointeur.
    const nvrhi::BindingSetHandle brdfBindings = device.createBindingSet(
        nvrhi::BindingSetDesc().addItem(nvrhi::BindingSetItem::Texture_UAV(1, environment.brdfLut)),
        brdf->layout);
    nvrhi::ComputeState brdfState;
    brdfState.pipeline = brdf->pipeline;
    brdfState.addBindingSet(brdfBindings);
    commandList->setComputeState(brdfState);
    const std::uint32_t brdfGroups = (BrdfLutSize + TexelsPerGroupSide - 1) / TexelsPerGroupSide;
    commandList->dispatch(brdfGroups, brdfGroups);
    commandList->close();
    // Les textures et les passes locales peuvent disparaître après l'envoi : NVRHI et Dawn gardent
    // ce qu'une command list utilise jusqu'à la fin de son exécution.
    device.executeCommandList(commandList);
    return environment;
}

core::Result<Environment> createUniformEnvironment(nvrhi::IDevice& device, glm::vec3 radiance)
{
    const std::array pixel{radiance.r, radiance.g, radiance.b, 1.0f};
    return createEnvironment(device, {.width = 1, .height = 1, .rgba = pixel}, 1);
}

std::optional<Sun> extractSun(std::uint32_t width, std::uint32_t height, std::span<float> rgba)
{
    LEVAIN_ASSERT(rgba.size() == std::size_t{width} * height * 4, "quatre flottants par pixel");
    const auto colorAt = [&rgba](std::size_t pixel)
    { return glm::vec3{rgba[pixel * 4], rgba[(pixel * 4) + 1], rgba[(pixel * 4) + 2]}; };
    const auto uvOf = [width, height](std::size_t pixel)
    {
        const std::size_t row = pixel / width; // la division entière : l'indice de la ligne
        return glm::vec2{(static_cast<float>(pixel % width) + 0.5f) / static_cast<float>(width),
                         (static_cast<float>(row) + 0.5f) / static_cast<float>(height)};
    };
    const std::size_t pixels = std::size_t{width} * height;
    std::size_t peak = 0;
    float peakLuminance = 0.0f;
    double total = 0.0;
    for (std::size_t pixel = 0; pixel < pixels; ++pixel)
    {
        const float luminance = luminanceOf(colorAt(pixel));
        total += luminance;
        if (luminance > peakLuminance)
        {
            peak = pixel;
            peakLuminance = luminance;
        }
    }
    const auto mean = static_cast<float>(total / static_cast<double>(pixels));
    if (!(peakLuminance > SunContrast * mean))
    {
        return std::nullopt;
    }

    const float threshold = peakLuminance * SunThresholdRatio;
    const glm::vec3 peakDirection = equirectDirectionOf(uvOf(peak));
    const float minimumCosine = std::cos(glm::radians(SunRadiusDegrees));
    // Un pixel couvre un angle solide qui rétrécit vers les pôles, comme le sinus de sa latitude.
    const float pixelArea = 2.0f * std::numbers::pi_v<float> / static_cast<float>(width) *
                            std::numbers::pi_v<float> / static_cast<float>(height);
    glm::dvec3 energy{0.0};
    glm::dvec3 towardSun{0.0};
    for (std::size_t pixel = 0; pixel < pixels; ++pixel)
    {
        const glm::vec3 color = colorAt(pixel);
        const float luminance = luminanceOf(color);
        const glm::vec3 direction = equirectDirectionOf(uvOf(pixel));
        if (luminance <= threshold || glm::dot(direction, peakDirection) < minimumCosine)
        {
            continue;
        }
        const glm::vec3 kept = color * (threshold / luminance);
        const float solidAngle = pixelArea * std::sqrt(1.0f - (direction.y * direction.y));
        energy += glm::dvec3{(color - kept) * solidAngle};
        towardSun += glm::dvec3{direction * ((luminance - threshold) * solidAngle)};
        std::copy_n(&kept.x, 3, rgba.begin() + static_cast<std::ptrdiff_t>(pixel * 4));
    }
    const glm::vec3 irradiance{energy};
    const float intensity = luminanceOf(irradiance);
    return Sun{.direction = glm::normalize(glm::vec3{towardSun}),
               .color = irradiance / intensity,
               .intensity = intensity};
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
