// L'UI d'ImGui dessinée par NVRHI (ADR-0032), sur Vulkan ou sur le backend WebGPU (Dawn). Un
// rectangle opaque de couleur connue, dans une cible sRGB puis dans une cible UNORM : relu, il doit
// garder sa couleur dans les deux. Une cible sRGB sans linéarisation donnerait une couleur délavée
// (`linearOnSrgbTarget`). Le rectangle de découpe déborde de l'image : borné, il ne doit faire
// aucune erreur de validation (`clampScissorToTarget`). L'atlas des polices passe par le chemin des
// textures d'ImGui 1.92 : sans lui, le rectangle, qui lit son pixel blanc, ne se dessinerait pas.
// Puis des textures que l'UI ne possède pas (`registerUiTexture`, ADR-0036 morceau 6), montrées par
// `ImGui::Image` : un gris moyen (0,5 linéaire, 188 en sRGB) relu à ±2 près, qui ne doit pas être
// converti deux fois, la table des identifiants, et une image libérée encore en vol.
//   levain_ui_gpu [vulkan|webgpu]

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <initializer_list>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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

/// Le niveau sRGB de 8 bits d'une valeur linéaire : ce qu'écrit une cible sRGB, la fonction exacte
/// de l'IEC 61966-2-1. Le test ne la prend pas du shader qu'il vérifie.
int srgbLevel(float linear)
{
    const float encoded =
        linear <= 0.0031308f ? linear * 12.92f : (1.055f * std::pow(linear, 1.0f / 2.4f)) - 0.055f;
    return static_cast<int>(std::lround(encoded * 255.0f));
}

/// Le gris moyen de la spécification : 0,5 linéaire s'écrit 188 dans une cible sRGB.
constexpr int GreyLevel = 188;

/// Une image à montrer : une texture de `format`, effacée à `clear` **dans les valeurs du format**
/// (un gris linéaire pour une texture sRGB, que l'effacement encode ; la valeur brute pour une
/// UNORM).
struct GreyImage
{
    nvrhi::Format format;
    float clear;
};

/// Quand `releaseUiTexture` passe, dans la vie d'une image.
enum class Release : std::uint8_t
{
    Never,          ///< À la fin, avec la passe (`destroyUiTextures`).
    AfterRecording, ///< Après `recordUi`, avant la soumission : la Vue lâche l'ancienne image.
};

constexpr int ImageSize = 32;
constexpr int ImageStep = 40;
constexpr int ImageMargin = 8;

/// Le rouge relu au centre de chaque image, et ce que la passe a dessiné.
struct ImagesDrawn
{
    std::vector<int> levels;
    levain::ui::UiDrawStats stats;
};

/// Des drapeaux de fenêtre d'ImGui réunis en non signé : leur `|` sur des `int` est refusé par
/// `bugprone-signed-bitwise`.
ImGuiWindowFlags windowFlags(std::initializer_list<ImGuiWindowFlags_> flags)
{
    unsigned combined = 0;
    for (const ImGuiWindowFlags_ flag : flags)
    {
        combined |= static_cast<unsigned>(flag);
    }
    return static_cast<ImGuiWindowFlags>(combined);
}

/// Montre chaque texture par une `ImGui::Image` (32 × 32, côte à côte) dans une cible de
/// `targetFormat`, et relit le centre de chacune. Rien si le GPU refuse quelque chose, ou si les
/// trois canaux d'un centre diffèrent (une image grise reste grise).
std::optional<ImagesDrawn> drawImages(nvrhi::IDevice& device, nvrhi::Format targetFormat,
                                      std::span<const GreyImage> images, Release release)
{
    const nvrhi::TextureHandle target = createTarget(device, targetFormat);
    const nvrhi::FramebufferHandle framebuffer =
        device.createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(target));
    auto pass = levain::ui::createUiPass(device, framebuffer->getFramebufferInfo());
    if (!pass)
    {
        std::println(stderr, "{}", pass.error().message);
        return std::nullopt;
    }
    // Des textures comme la Vue en aura : cible de rendu, lue ensuite par un shader, dans cet état
    // au repos (`registerUiTexture`). La valeur d'effacement est exigée par Direct3D 12.
    std::vector<nvrhi::TextureHandle> textures;
    std::vector<ImTextureID> ids;
    for (const GreyImage& image : images)
    {
        const nvrhi::Color clear(image.clear, image.clear, image.clear, 1.0f);
        const nvrhi::TextureHandle& texture = textures.emplace_back(
            device.createTexture(nvrhi::TextureDesc()
                                     .setWidth(ImageSize)
                                     .setHeight(ImageSize)
                                     .setFormat(image.format)
                                     .setIsRenderTarget(true)
                                     .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                     .setKeepInitialState(true)
                                     .setClearValue(clear)
                                     .setUseClearValue(true)
                                     .setDebugName("image grise")));
        const auto id = levain::ui::registerUiTexture(device, *pass, texture);
        if (!id)
        {
            std::println(stderr, "{}", id.error().message);
            return std::nullopt;
        }
        ids.push_back(*id);
    }

    const levain::ui::UiContext context = levain::ui::createUiContext(1.0f);
    levain::ui::prepareUiFrame(ImGui::GetIO(), {.width = Width, .height = Height}, 1.0 / 60.0);
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(Width), static_cast<float>(Height)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("images", nullptr,
                 windowFlags({ImGuiWindowFlags_NoDecoration, ImGuiWindowFlags_NoBackground,
                              ImGuiWindowFlags_NoSavedSettings, ImGuiWindowFlags_NoInputs}));
    for (std::size_t index = 0; index < ids.size(); ++index)
    {
        ImGui::SetCursorPos(
            ImVec2(static_cast<float>(ImageMargin + (static_cast<int>(index) * ImageStep)),
                   static_cast<float>(ImageMargin)));
        ImGui::Image(ids[index],
                     ImVec2(static_cast<float>(ImageSize), static_cast<float>(ImageSize)));
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::Render();

    // Les gris s'écrivent dans une command list à part, finie et retirée avant la suivante : le
    // `clearTextureFloat` de Direct3D 12 garde la texture dans les ressources de sa command list
    // (NVRHI, `d3d12-texture.cpp`), et ferait tenir l'image en vol sans le binding set que le cas
    // `AfterRecording` veut isoler.
    {
        const nvrhi::CommandListHandle fill = device.createCommandList();
        fill->open();
        for (std::size_t index = 0; index < textures.size(); ++index)
        {
            const float grey = images[index].clear;
            fill->clearTextureFloat(textures[index], nvrhi::AllSubresources,
                                    nvrhi::Color(grey, grey, grey, 1.0f));
        }
        fill->close();
        device.executeCommandList(fill);
    }
    device.waitForIdle();
    device.runGarbageCollection();

    const nvrhi::CommandListHandle commandList = device.createCommandList();
    commandList->open();
    commandList->clearTextureFloat(target, nvrhi::AllSubresources, nvrhi::Color(0.0f));
    ImagesDrawn drawn;
    drawn.stats =
        levain::ui::recordUi(device, *commandList, *pass, *ImGui::GetDrawData(), *framebuffer);
    const nvrhi::StagingTextureHandle staging =
        levain::render::copyForReadback(device, *commandList, *target);
    if (release == Release::AfterRecording)
    {
        // L'image est dans la command list, pas encore soumise : on lâche la table et le programme.
        // Seul le binding set que la command list tient garde la texture (`releaseUiTexture`) : les
        // gris sont écrits plus haut, par une command list déjà retirée.
        for (const ImTextureID id : ids)
        {
            levain::ui::releaseUiTexture(*pass, id);
        }
        textures.clear();
    }
    commandList->close();
    device.executeCommandList(commandList);
    levain::ui::destroyUiTextures(*pass, ImGui::GetPlatformIO().Textures);

    auto image = levain::render::readBack(device, *staging);
    if (!image)
    {
        std::println(stderr, "{}", image.error().message);
        return std::nullopt;
    }
    for (std::size_t index = 0; index < ids.size(); ++index)
    {
        const std::size_t x = ImageMargin + (index * ImageStep) + (ImageSize / 2);
        const std::size_t pixel = (((ImageMargin + (ImageSize / 2)) * std::size_t{Width}) + x) * 4;
        const int red = image->rgba[pixel];
        if (std::abs(red - image->rgba[pixel + 1]) > 1 ||
            std::abs(red - image->rgba[pixel + 2]) > 1)
        {
            std::println(stderr, "image {} : ({}, {}, {}) n'est pas grise", index, red,
                         image->rgba[pixel + 1], image->rgba[pixel + 2]);
            return std::nullopt;
        }
        drawn.levels.push_back(red);
    }
    return drawn;
}

/// Un gris moyen par `ImGui::Image`, dans chaque combinaison du format de la texture et de celui de
/// la cible. La cible convertit une fois en écrivant (`linearOnSrgbTarget`) : la passe ne touche
/// pas aux texels, et rien ne doit être converti deux fois.
int checkGreyAcrossFormats(nvrhi::IDevice& device)
{
    struct Case
    {
        std::string_view name;
        nvrhi::Format target;
        GreyImage image;
        bool convertedTwice; ///< Le témoin : le défaut qu'on veut voir, pas un cas juste.
    };

    constexpr float EncodedGrey = 188.0f / 255.0f;
    const std::array<Case, 4> cases{{
        {"sRGB sur sRGB : le matériel décode, la cible encode",
         nvrhi::Format::SRGBA8_UNORM,
         {nvrhi::Format::SRGBA8_UNORM, 0.5f},
         false},
        {"UNORM linéaire sur sRGB : la cible encode",
         nvrhi::Format::SRGBA8_UNORM,
         {nvrhi::Format::RGBA8_UNORM, 0.5f},
         false},
        {"UNORM encodée sur UNORM : rien ne convertit",
         nvrhi::Format::RGBA8_UNORM,
         {nvrhi::Format::RGBA8_UNORM, EncodedGrey},
         false},
        {"témoin, UNORM encodée sur sRGB : convertie deux fois",
         nvrhi::Format::SRGBA8_UNORM,
         {nvrhi::Format::RGBA8_UNORM, EncodedGrey},
         true},
    }};
    // Le garde de l'attendu : si `srgbLevel` dérivait, tout le reste dériverait avec lui.
    int failures = srgbLevel(0.5f) == GreyLevel ? 0 : 1;
    for (const Case& test : cases)
    {
        const auto drawn =
            drawImages(device, test.target, std::span{&test.image, 1}, Release::Never);
        if (!drawn || drawn->stats.draws != 1)
        {
            std::println(stderr, "{} : rien de dessiné", test.name);
            ++failures;
            continue;
        }
        const int level = drawn->levels.front();
        const int gap = std::abs(level - GreyLevel);
        std::println("{} : {} relu pour {}", test.name, level, GreyLevel);
        // Le témoin doit s'éloigner de 188 de plus de 20 niveaux : sans cela, le test ne verrait
        // pas une double conversion (223 mesuré).
        if (test.convertedTwice ? gap < 20 : gap > 2)
        {
            std::println(stderr, "{} : écart de {} niveaux", test.name, gap);
            ++failures;
        }
    }
    return failures;
}

/// Plusieurs textures dans la même image, de valeurs différentes : chaque commande lit la sienne.
/// Puis l'image libérée alors que la command list qui la dessine n'est pas soumise (l'ancienne
/// image de la Vue, libérée après `recordUi`) : elle se voit encore, et la validation ne dit rien.
int checkSeveralAndInFlight(nvrhi::IDevice& device)
{
    int failures = 0;
    const std::array<GreyImage, 3> images{{{nvrhi::Format::SRGBA8_UNORM, 0.5f},
                                           {nvrhi::Format::SRGBA8_UNORM, 0.1f},
                                           {nvrhi::Format::RGBA8_UNORM, 0.5f}}};
    const std::array<int, 3> expected{GreyLevel, srgbLevel(0.1f), GreyLevel};
    for (const Release release : {Release::Never, Release::AfterRecording})
    {
        const auto drawn = drawImages(device, nvrhi::Format::SRGBA8_UNORM, images, release);
        if (!drawn || drawn->stats.draws != 3)
        {
            std::println(stderr, "plusieurs images : rien de dessiné");
            ++failures;
            continue;
        }
        for (std::size_t index = 0; index < images.size(); ++index)
        {
            std::println("image {} ({}) : {} relu pour {}", index,
                         release == Release::Never ? "tenue" : "libérée en vol",
                         drawn->levels[index], expected[index]);
            if (std::abs(drawn->levels[index] - expected[index]) > 2)
            {
                std::println(stderr, "image {} : écart de {} niveaux", index,
                             std::abs(drawn->levels[index] - expected[index]));
                ++failures;
            }
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
    // Le refus de la description (`uiTextureRefusal`) est bien appelé par `registerUiTexture` :
    // sans lui, ces textures feraient une erreur de validation, à la création du bind group
    // (WebGPU, flottants sur 32 bits : une assertion en Debug) ou au premier dessin (Vulkan, format
    // entier).
    const auto refusedAtRegistration = [&](std::string_view what, nvrhi::TextureDesc desc)
    {
        const nvrhi::TextureHandle texture =
            device.createTexture(desc.setWidth(4).setHeight(4).setDebugName("image refusée"));
        expect(texture != nullptr, std::string{what} + " : texture non créée");
        if (texture)
        {
            const auto refused = levain::ui::registerUiTexture(device, *pass, texture);
            expect(!refused && refused.error().code == levain::core::ErrorCode::Unsupported,
                   std::string{what} + " : refusée à l'enregistrement");
        }
        expect(pass->textures.size() == 2, std::string{what} + " : rien dans la table");
    };
    const auto color = nvrhi::TextureDesc().setFormat(nvrhi::Format::RGBA8_UNORM);
    refusedAtRegistration(
        "une profondeur",
        nvrhi::TextureDesc(color).setFormat(nvrhi::Format::D32).setIsRenderTarget(true));
    // NVRHI ne crée une texture multi-échantillon qu'en `Texture2DMS`.
    refusedAtRegistration("une texture multi-échantillon",
                          nvrhi::TextureDesc(color)
                              .setDimension(nvrhi::TextureDimension::Texture2DMS)
                              .setSampleCount(4)
                              .setIsRenderTarget(true));
    // R32_UINT : le seul entier que le backend WebGPU sait créer.
    refusedAtRegistration("un format entier",
                          nvrhi::TextureDesc(color).setFormat(nvrhi::Format::R32_UINT));
    refusedAtRegistration("des flottants sur 32 bits",
                          nvrhi::TextureDesc(color).setFormat(nvrhi::Format::RGBA32_FLOAT));
    // Direct3D 12 ne crée pas une texture de couleur sans usage de shader (DENY_SHADER_RESOURCE
    // exige une profondeur : D3D12_MESSAGE_ID 599, une assertion en Debug) : le cas ne s'y
    // construit pas, et le doctest de `uiTextureRefusal` garde la règle pour tous les backends.
    if (device.getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12)
    {
        std::println("une texture que le shader ne lit pas : non constructible sous Direct3D 12");
    }
    else
    {
        nvrhi::TextureDesc unreadable = nvrhi::TextureDesc(color).setIsRenderTarget(true);
        unreadable.isShaderResource = false;
        refusedAtRegistration("une texture que le shader ne lit pas", unreadable);
    }
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
    return compareTargets(device) + checkGreyAcrossFormats(device) +
           checkSeveralAndInFlight(device) + checkIdTable(device);
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
