// L'UI d'ImGui dessinée par NVRHI (ADR-0032), sur Vulkan ou sur le backend WebGPU (Dawn). Un
// rectangle opaque de couleur connue, dans une cible sRGB puis dans une cible UNORM : relu, il doit
// garder sa couleur dans les deux. Une cible sRGB sans linéarisation donnerait une couleur délavée
// (`linearOnSrgbTarget`). Le rectangle de découpe déborde de l'image : borné, il ne doit faire
// aucune erreur de validation (`clampScissorToTarget`). L'atlas des polices passe par le chemin des
// textures d'ImGui 1.92 : sans lui, le rectangle, qui lit son pixel blanc, ne se dessinerait pas.
// Puis la table des textures que l'UI ne possède pas (`registerUiTexture`, ADR-0036 morceau 6) :
// enregistrer, libérer, enregistrer encore.
//   levain_ui_gpu [vulkan|webgpu]

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <print>
#include <span>
#include <string>
#include <string_view>

#include <imgui.h>

#include "gpu_test_backend.hpp"

#include "levain/core/assert.hpp"
#include "levain/core/error.hpp"
#include "levain/gpu/device.hpp"
#include "levain/gpu/webgpu.hpp"
#include "levain/platform/window.hpp"
#include "levain/render/readback.hpp"
#include "levain/ui/context.hpp"
#include "levain/ui/ui_pass.hpp"

namespace
{

constexpr std::uint32_t Width = 128;
constexpr std::uint32_t Height = 64;

/// La couleur du rectangle, en sRGB, comme ImGui la donne.
constexpr std::uint8_t Red = 200;
constexpr std::uint8_t Green = 100;
constexpr std::uint8_t Blue = 50;

/// La cible de l'UI : une texture de `format` où l'on dessine, effacée en noir transparent.
nvrhi::TextureHandle createTarget(nvrhi::IDevice& device, nvrhi::Format format)
{
    return device.createTexture(nvrhi::TextureDesc()
                                    .setWidth(Width)
                                    .setHeight(Height)
                                    .setFormat(format)
                                    .setIsRenderTarget(true)
                                    .setInitialState(nvrhi::ResourceStates::RenderTarget)
                                    .setKeepInitialState(true)
                                    .setClearValue(nvrhi::Color(0.0f))
                                    .setUseClearValue(true)
                                    .setDebugName("cible de l'UI"));
}

/// Dessine le rectangle dans une cible de `format`, et rend l'écart le plus grand, en niveaux,
/// entre la couleur relue en son centre et celle demandée ; -1 si rien ne s'est dessiné.
int drawAndCompare(nvrhi::IDevice& device, nvrhi::Format format)
{
    const nvrhi::TextureHandle texture = createTarget(device, format);
    const nvrhi::FramebufferHandle target =
        device.createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(texture));
    auto pass = levain::ui::createUiPass(device, target->getFramebufferInfo());
    if (!pass)
    {
        std::println(stderr, "{}", pass.error().message);
        return -1;
    }

    const levain::ui::UiContext context = levain::ui::createUiContext(1.0f);
    ImGuiIO& io = ImGui::GetIO();
    levain::ui::prepareUiFrame(io, {.width = Width, .height = Height}, 1.0 / 60.0);
    ImGui::NewFrame();
    ImDrawList* list = ImGui::GetForegroundDrawList();
    // Une découpe qui déborde de partout : à borner, sinon erreur de validation.
    list->PushClipRect(ImVec2(-50.0f, -50.0f), ImVec2(500.0f, 500.0f), false);
    list->AddRectFilled(ImVec2(16.0f, 16.0f), ImVec2(48.0f, 48.0f),
                        IM_COL32(Red, Green, Blue, 255));
    // Un triangle lissé : 21 index, un nombre impair, que l'écriture du buffer d'index doit
    // arrondir sans lire au-delà (`roundToFourBytes`, vérifié par ASan en CI).
    list->AddTriangleFilled(ImVec2(80.0f, 10.0f), ImVec2(120.0f, 10.0f), ImVec2(100.0f, 50.0f),
                            IM_COL32(255, 255, 255, 255));
    list->PopClipRect();
    ImGui::Render();

    const nvrhi::CommandListHandle commandList = device.createCommandList();
    commandList->open();
    commandList->clearTextureFloat(texture, nvrhi::AllSubresources, nvrhi::Color(0.0f));
    const levain::ui::UiDrawStats stats =
        levain::ui::recordUi(device, *commandList, *pass, *ImGui::GetDrawData(), *target);
    const nvrhi::StagingTextureHandle staging =
        levain::render::copyForReadback(device, *commandList, *texture);
    commandList->close();
    device.executeCommandList(commandList);
    levain::ui::destroyUiTextures(*pass, ImGui::GetPlatformIO().Textures);

    auto image = levain::render::readBack(device, *staging);
    if (!image)
    {
        std::println(stderr, "{}", image.error().message);
        return -1;
    }
    if (stats.draws == 0)
    {
        std::println(stderr, "l'UI n'a rien dessiné");
        return -1;
    }
    const std::size_t pixel = ((std::size_t{32} * Width) + 32) * 4;
    const auto gap = [&](std::size_t channel, std::uint8_t expected)
    { return std::abs(int{image->rgba[pixel + channel]} - int{expected}); };
    const int worst = std::max({gap(0, Red), gap(1, Green), gap(2, Blue)});
    std::println("{} : ({}, {}, {}) relu pour ({}, {}, {}), {} commandes, {} découpées",
                 nvrhi::getFormatInfo(format).name, image->rgba[pixel], image->rgba[pixel + 1],
                 image->rgba[pixel + 2], Red, Green, Blue, stats.draws, stats.clipped);
    return worst;
}

/// Les deux cibles : une couleur à plus de deux niveaux de celle demandée est un échec.
int compareTargets(nvrhi::IDevice& device)
{
    int failures = 0;
    for (const nvrhi::Format format : {nvrhi::Format::SRGBA8_UNORM, nvrhi::Format::RGBA8_UNORM})
    {
        const int worst = drawAndCompare(device, format);
        if (worst < 0 || worst > 2)
        {
            std::println(stderr, "{} : écart de {} niveaux", nvrhi::getFormatInfo(format).name,
                         worst);
            ++failures;
        }
    }
    return failures;
}

/// Une petite texture que l'UI sait montrer, pour les tests de la table des identifiants : ils
/// n'en lisent pas les texels.
nvrhi::TextureHandle tinyTexture(nvrhi::IDevice& device)
{
    return device.createTexture(nvrhi::TextureDesc()
                                    .setWidth(4)
                                    .setHeight(4)
                                    .setFormat(nvrhi::Format::RGBA8_UNORM)
                                    .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                    .setKeepInitialState(true)
                                    .setDebugName("image de la table"));
}

/// La table des identifiants : enregistrer, libérer, enregistrer encore. Un identifiant libéré ne
/// revient pas (`UiPass::nextTextureId` ne recule jamais), et libérer l'un n'enlève pas l'autre.
int checkIdTable(nvrhi::IDevice& device)
{
    const nvrhi::TextureHandle target = createTarget(device, nvrhi::Format::SRGBA8_UNORM);
    const nvrhi::FramebufferHandle framebuffer =
        device.createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(target));
    auto pass = levain::ui::createUiPass(device, framebuffer->getFramebufferInfo());
    if (!pass)
    {
        std::println(stderr, "{}", pass.error().message);
        return 1;
    }
    int failures = 0;
    const auto expect = [&](bool condition, const std::string& what)
    {
        if (!condition)
        {
            std::println(stderr, "table des identifiants : {}", what);
            ++failures;
        }
    };
    const nvrhi::TextureHandle first = tinyTexture(device);
    const nvrhi::TextureHandle second = tinyTexture(device);
    const auto idFirst = levain::ui::registerUiTexture(device, *pass, first);
    const auto idSecond = levain::ui::registerUiTexture(device, *pass, second);
    if (!idFirst || !idSecond)
    {
        std::println(stderr, "table des identifiants : enregistrement refusé");
        return 1;
    }
    expect(*idFirst != ImTextureID_Invalid && *idFirst != *idSecond, "deux identifiants distincts");
    expect(pass->textures.size() == 2, "deux textures dans la table");

    levain::ui::releaseUiTexture(*pass, *idFirst);
    expect(!pass->textures.contains(*idFirst) && pass->textures.contains(*idSecond),
           "libérer l'une laisse l'autre");
    const auto idAgain = levain::ui::registerUiTexture(device, *pass, first);
    expect(idAgain && *idAgain != *idFirst && *idAgain != *idSecond,
           "l'identifiant libéré ne revient pas");
    expect(pass->textures.size() == 2, "deux textures après le nouvel enregistrement");

    // Les refus sont des erreurs, pas des assertions : le programme les reçoit.
    expect(!levain::ui::registerUiTexture(device, *pass, nullptr), "une texture nulle est refusée");
    expect(pass->textures.size() == 2, "un refus ne laisse rien dans la table");
#if !LEVAIN_ASSERTIONS_ENABLED
    // En Debug, libérer deux fois s'arrête sur l'assertion : seule la Release rend la main, et
    // dit l'erreur au journal sans toucher à la table.
    levain::ui::releaseUiTexture(*pass, *idFirst);
    levain::ui::releaseUiTexture(*pass, 12345);
    expect(pass->textures.size() == 2, "libérer deux fois, ou l'inconnu, ne change rien");
    // L'atlas des polices est à ImGui : le libérer ici est la même erreur, il reste dans la table.
    const levain::ui::UiContext context = levain::ui::createUiContext(1.0f);
    levain::ui::prepareUiFrame(ImGui::GetIO(), {.width = Width, .height = Height}, 1.0 / 60.0);
    ImGui::NewFrame();
    ImGui::Render();
    const nvrhi::CommandListHandle commandList = device.createCommandList();
    commandList->open();
    levain::ui::updateUiTextures(device, *commandList, *pass, ImGui::GetPlatformIO().Textures);
    commandList->close();
    device.executeCommandList(commandList);
    ImTextureID atlas = ImTextureID_Invalid;
    for (const auto& [id, texture] : pass->textures)
    {
        atlas = texture.registered ? atlas : id;
    }
    expect(atlas != ImTextureID_Invalid && pass->textures.size() == 3, "l'atlas est dans la table");
    levain::ui::releaseUiTexture(*pass, atlas);
    expect(pass->textures.contains(atlas), "l'atlas d'ImGui n'est pas libéré par le programme");
    levain::ui::destroyUiTextures(*pass, ImGui::GetPlatformIO().Textures);
#endif
    std::println("table des identifiants : {} échec(s)", failures);
    return failures;
}

int check(nvrhi::IDevice& device)
{
    return compareTargets(device) + checkIdTable(device);
}

int run(const levain::tests::TestBackend& backend)
{
    if (backend.api == nvrhi::GraphicsAPI::WEBGPU)
    {
        auto device = levain::gpu::createWebGpuDevice({.enableValidation = true});
        if (!device)
        {
            std::println(stderr, "{}", device.error().message);
            return 1;
        }
        return check(**device) == 0 ? 0 : 1;
    }
    auto window =
        levain::platform::createWindow("Levain - UI", 64, 64, levain::gpu::surfaceFor(backend.api));
    if (!window)
    {
        std::println(stderr, "{}", window.error().message);
        return 1;
    }
    auto gpu = levain::gpu::createGpuDevice(*window, levain::tests::testDeviceOptions(backend));
    if (!gpu)
    {
        std::println(stderr, "{}", gpu.error().message);
        return 1;
    }
    return check(*gpu->nvrhi) == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        const std::span arguments{argv, static_cast<std::size_t>(argc)};
        const auto backend =
            levain::tests::testBackendNamed(arguments.size() == 2 ? arguments[1] : "vulkan");
        if (arguments.size() > 2 || !backend)
        {
            std::println(stderr, "usage : levain_ui_gpu [vulkan|d3d12|d3d12-warp|webgpu]");
            return 2;
        }
        return run(*backend);
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
