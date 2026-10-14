// La scène dans une texture, dans la vraie boucle (ADR-0036, morceau 7). Un programme minimal joue
// la Vue : dans `ui`, il demande une taille (`requestScene`) et montre l'image rendue par une
// `ImGui::Image`, comme le fera la fenêtre du morceau 8. Une fonction d'étape inscrite chez le
// renderer note la cible et l'aspect de chaque scène rendue ; la texture de la scène et la capture
// (la swapchain) sont relues après la boucle. Il faut un device, comme `levain_app_loop`.
//   levain_scene_view [vulkan|d3d12|d3d12-warp]

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <functional>
#include <initializer_list>
#include <optional>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>
// Le nœud de l'espace d'ancrage, pour lire ses drapeaux : l'API interne d'ImGui.
#include <imgui_internal.h>

#include "gpu_test_backend.hpp"

#include "levain/app/app.hpp"
#include "levain/assets/asset_id.hpp"
#include "levain/assets/image.hpp"
#include "levain/core/color.hpp"
#include "levain/render/readback.hpp"
#include "levain/render/stages.hpp"
#include "levain/scene/components.hpp"

namespace
{

using levain::app::App;
using levain::app::SceneSize;

/// La fenêtre du test : 320 × 180. La « Vue » montre son image en (10, 10), à 150 × 84.
constexpr std::uint32_t WindowWidth = 320;
constexpr std::uint32_t WindowHeight = 180;
constexpr float ViewX = 10.0f;
constexpr float ViewY = 10.0f;
constexpr float ViewWidth = 150.0f;
constexpr float ViewHeight = 84.0f;
/// Un point de la Vue, un point de la fenêtre qu'aucune image ne couvre.
constexpr std::size_t InsideX = 85;
constexpr std::size_t InsideY = 52;
constexpr std::size_t OutsideX = 300;
constexpr std::size_t OutsideY = 170;

/// Une scène rendue : la cible de l'image HDR (sa taille est celle de la sortie du renderer) et
/// l'aspect de la projection que le renderer a construite.
struct Rendered
{
    int frame = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    float aspect = 0.0f;
};

/// Ce que la Vue a demandé dans `ui`, et ce que la boucle lui a rendu.
struct Asked
{
    int frame = 0;
    SceneSize size{};
    bool gotImage = false;
    int created = 0; ///< `SceneView::created`, juste après la demande
    /// L'espace d'ancrage de cette image laisse-t-il passer son centre (`PassthruCentralNode`) ?
    /// Il le doit tant que la scène est dans la fenêtre, derrière lui.
    bool passthru = false;
    bool dockspace = false; ///< l'espace d'ancrage existe (panneaux ouverts)
};

struct Pixel
{
    std::array<std::uint8_t, 4> rgba{};

    [[nodiscard]] int channel(std::size_t index) const { return rgba.at(index); }
};

struct Run
{
    int exitCode = 0;
    std::vector<Rendered> rendered;
    std::vector<Asked> asked;
    int created = 0;
    /// Les textures que la table de l'UI tient pour le programme à la fin : celle de la scène,
    /// et plus aucune des anciennes.
    std::ptrdiff_t registeredTextures = 0;
    /// L'image que la Vue a reçue à l'image d'avant, 0 : aucune.
    ImTextureID lastImage = 0;
    /// Les images où la Vue a reçu une image neuve, remplaçant celle d'avant ; et, parmi elles,
    /// celles où l'ancienne était encore dans la table de l'UI, en fin de `requestScene`.
    int replacements = 0;
    int replacedStillRegistered = 0;
    SceneSize texture{}; ///< la texture de la scène à la fin de la boucle
    Pixel textureCenter;
    std::optional<levain::assets::Image> capture;
};

/// Un dossier à soi pour la capture, effacé à la fin.
struct TempDir
{
    std::filesystem::path path;

    TempDir()
        : path{std::filesystem::temp_directory_path() /
               ("levain-scene-view-" + levain::assets::toString(levain::assets::generateAssetId()))}
    {
        std::filesystem::create_directories(path);
    }

    ~TempDir()
    {
        std::error_code ignored; // un destructeur ne lève pas : le dossier reste dans le temporaire
        std::filesystem::remove_all(path, ignored);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
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

/// Ce que la Vue demande à l'image `frame`, `panelsOpen` étant l'état de F1. Rien : la scène reste
/// dans la fenêtre.
using Ask = std::function<std::optional<SceneSize>(int frame, bool panelsOpen)>;

/// Le dessin de la fenêtre de la Vue : une image, sans décor ni marge, qui n'a pas le focus.
void drawView(ImTextureID image)
{
    ImGui::SetNextWindowPos(ImVec2(ViewX, ViewY));
    ImGui::SetNextWindowSize(ImVec2(ViewWidth, ViewHeight));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("###vue", nullptr,
                 windowFlags({ImGuiWindowFlags_NoDecoration, ImGuiWindowFlags_NoBackground,
                              ImGuiWindowFlags_NoSavedSettings, ImGuiWindowFlags_NoInputs}));
    ImGui::Image(image, ImVec2(ViewWidth, ViewHeight));
    ImGui::End();
    ImGui::PopStyleVar();
}

/// Une seconde image, que `ui` dessine avec l'identifiant de l'image d'avant, après avoir demandé
/// une taille neuve : un autre panneau de la même image, ou un dessin déjà posé, la montre encore.
/// L'ancienne image doit exister jusqu'à l'enregistrement de l'UI de cette image (Debug : sinon
/// l'assertion d'identifiant inconnu de `recordUi` arrête le programme).
void drawStale(ImTextureID image)
{
    ImGui::SetNextWindowPos(ImVec2(ViewX + ViewWidth + 10.0f, ViewY));
    ImGui::SetNextWindowSize(ImVec2(40.0f, 22.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("###ancienne", nullptr,
                 windowFlags({ImGuiWindowFlags_NoDecoration, ImGuiWindowFlags_NoBackground,
                              ImGuiWindowFlags_NoSavedSettings, ImGuiWindowFlags_NoInputs}));
    ImGui::Image(image, ImVec2(40.0f, 22.0f));
    ImGui::End();
    ImGui::PopStyleVar();
}

/// L'espace d'ancrage des panneaux laisse-t-il passer le centre cette image ? Les drapeaux que
/// `drawEnginePanels` lui a donnés, relus sur son nœud.
bool dockspacePassesThrough(bool& exists)
{
    const ImGuiDockNode* node = ImGui::DockBuilderGetNode(ImHashStr("levain"));
    exists = node != nullptr;
    return node != nullptr && (static_cast<unsigned>(node->SharedFlags) &
                               static_cast<unsigned>(ImGuiDockNodeFlags_PassthruCentralNode)) != 0;
}

/// Relit le centre de la texture de la scène et sa taille, dans une command list à soi.
void readScene(App& app, Run& run)
{
    run.created = app.sceneView.created;
    run.registeredTextures = std::ranges::count_if(app.ui.pass.textures, [](const auto& entry)
                                                   { return entry.second.registered; });
    if (!app.sceneView.texture)
    {
        return;
    }
    nvrhi::IDevice& device = *app.gpu.nvrhi;
    const nvrhi::CommandListHandle commandList = device.createCommandList();
    commandList->open();
    const nvrhi::StagingTextureHandle staging =
        levain::render::copyForReadback(device, *commandList, *app.sceneView.texture);
    commandList->close();
    device.executeCommandList(commandList);
    const auto image = levain::render::readBack(device, *staging);
    if (!image)
    {
        throw std::runtime_error{image.error().message};
    }
    run.texture = {.width = image->width, .height = image->height};
    const std::size_t offset =
        ((std::size_t{image->height / 2} * image->width) + (image->width / 2)) * 4;
    std::copy_n(image->rgba.begin() + static_cast<std::ptrdiff_t>(offset), 4,
                run.textureCenter.rgba.begin());
}

levain::app::StartFunction programOf(Run& run, const Ask& ask)
{
    return [&run, &ask](App& app) -> levain::core::Result<levain::app::FrameHooks>
    {
        app.world.entity("camera").set(levain::scene::Transform{}).set(levain::app::CameraLens{});
        // Le témoin : la scène rendue, et ce que le renderer en a fait. Sa cible est l'image HDR,
        // à la taille de la sortie ; l'aspect se lit dans la projection.
        levain::render::addStageFunction(
            app.renderer.stages, levain::render::RenderStage::Opaque, "témoin",
            [&run, &app](const levain::render::StageContext& context)
            {
                const nvrhi::FramebufferInfoEx& target = context.target.getFramebufferInfo();
                const glm::mat4& matrix = context.viewProjection;
                run.rendered.push_back({.frame = app.frameCount,
                                        .width = target.width,
                                        .height = target.height,
                                        .aspect = std::abs(matrix[1][1] / matrix[0][0])});
            });
        levain::app::FrameHooks hooks;
        hooks.ui = [&run, &ask](App& uiApp)
        {
            const int frame = uiApp.frameCount;
            bool dockspace = false;
            const bool passthru = dockspacePassesThrough(dockspace);
            const std::optional<SceneSize> size = ask(frame, uiApp.ui.panelsOpen);
            if (!size)
            {
                return;
            }
            const auto image = levain::app::requestScene(uiApp, *size);
            run.asked.push_back({.frame = frame,
                                 .size = *size,
                                 .gotImage = image.has_value(),
                                 .created = uiApp.sceneView.created,
                                 .passthru = passthru,
                                 .dockspace = dockspace});
            if (image)
            {
                // Une image neuve : la Vue a été recréée dans cette image. L'ancienne, déjà retirée
                // de la Vue, tient encore dans la table de l'UI et dans le dessin d'un autre
                // panneau.
                if (run.lastImage != 0 && *image != run.lastImage)
                {
                    const auto entry = uiApp.ui.pass.textures.find(run.lastImage);
                    ++run.replacements;
                    run.replacedStillRegistered +=
                        entry != uiApp.ui.pass.textures.end() && entry->second.registered ? 1 : 0;
                    drawStale(run.lastImage);
                }
                drawView(*image);
            }
            run.lastImage = image.value_or(0);
        };
        hooks.finish = [&run](App& finishApp)
        {
            readScene(finishApp, run);
            return true;
        };
        return hooks;
    };
}

/// Le programme dans la vraie boucle, hors écran, `frames` images. `script` : l'input rejoué ;
/// `capture` : la dernière image (la swapchain) est écrite, puis relue.
Run play(const levain::tests::TestBackend& backend, const Ask& ask, int frames,
         std::string_view script = {}, bool capture = false)
{
    levain::app::AppSettings settings;
    if (!script.empty())
    {
        auto parsed = levain::platform::parseInputScript(script);
        if (!parsed)
        {
            throw std::runtime_error{parsed.error().message};
        }
        settings.inputScript = std::move(*parsed);
    }
    std::optional<TempDir> dir;
    if (capture)
    {
        dir.emplace();
        settings.capturePath = dir->path / "capture.png";
    }
    settings.steps = frames;
    settings.width = WindowWidth;
    settings.height = WindowHeight;
    settings.assetRoots = {LEVAIN_DATA_DIR};
    settings.bindingsFile = LEVAIN_DATA_DIR "/input.cfg";
    settings.api = backend.api;
    settings.adapter = backend.adapter;
    Run run;
    run.exitCode = levain::app::runApp(settings, programOf(run, ask));
    if (dir)
    {
        auto image = levain::assets::loadImage(dir->path / "capture.png");
        if (!image)
        {
            throw std::runtime_error{image.error().message};
        }
        run.capture = std::move(*image);
    }
    return run;
}

int expect(bool holds, std::string_view what)
{
    if (!holds)
    {
        std::println(stderr, "échec : {}", what);
    }
    return holds ? 0 : 1;
}

bool sameSize(const Rendered& rendered, const SceneSize& size)
{
    return rendered.width == size.width && rendered.height == size.height;
}

/// L'aspect de la projection est celui de la cible, à un millième près.
bool aspectFollows(const Rendered& rendered)
{
    return std::abs(rendered.aspect - static_cast<float>(rendered.width) /
                                          static_cast<float>(rendered.height)) < 1e-3f;
}

Pixel pixelOf(const levain::assets::Image& image, std::size_t x, std::size_t y)
{
    const std::size_t offset = ((y * image.width) + x) * 4;
    Pixel pixel;
    std::copy_n(image.rgba.begin() + static_cast<std::ptrdiff_t>(offset), 4, pixel.rgba.begin());
    return pixel;
}

/// Le fond de la scène est orange (`App::background`) : le rouge domine le bleu, ce qu'aucun gris
/// ne fait.
bool isScene(const Pixel& pixel)
{
    return pixel.channel(0) > pixel.channel(2) + 20;
}

/// Le gris dont la swapchain est effacée (`PresentClearLevel`, 0,02 linéaire), tel que l'image
/// finale le contient : la swapchain est UNORM sur tous les backends (décision 15), l'octet est
/// donc celui de l'encodage sRGB, quel que soit le backend.
int clearByte()
{
    return static_cast<int>(
        std::lround(levain::core::srgbFromLinear(levain::gpu::PresentClearLevel) * 255.0F));
}

bool isClear(const Pixel& pixel)
{
    return std::ranges::all_of(
        std::array{0, 1, 2}, [&](int channel)
        { return std::abs(pixel.channel(static_cast<std::size_t>(channel)) - clearByte()) <= 2; });
}

/// Une Vue de la taille demandée, 640 × 360 (celle de l'ADR) puis 400 × 100 : le renderer rend à
/// cette taille, avec l'aspect de cette taille, et non celui de la fenêtre (320 × 180). La texture
/// est créée à la première demande, tout de suite, et relue à la fin.
int renderedAtRequestedSize(const levain::tests::TestBackend& backend)
{
    int failures = 0;
    for (const SceneSize size :
         {SceneSize{.width = 640, .height = 360}, SceneSize{.width = 400, .height = 100}})
    {
        const std::string at = std::format("{} × {} : ", size.width, size.height);
        const Run run = play(backend, [size](int, bool) { return size; }, 4);
        failures += expect(run.exitCode == 0, at + "code 0");
        failures += expect(run.rendered.size() == 4, at + "une scène rendue par image");
        failures += expect(
            std::ranges::all_of(run.rendered, [&](const Rendered& r) { return sameSize(r, size); }),
            at + "le renderer rend à la taille demandée, pas à celle de la fenêtre");
        failures += expect(std::ranges::all_of(run.rendered, aspectFollows),
                           at + "l'aspect de la projection suit la taille demandée");
        failures += expect(run.created == 1 && run.texture == size && run.registeredTextures == 1,
                           at + "une seule texture, à la taille demandée");
        failures +=
            expect(isScene(run.textureCenter), at + "la scène a été rendue dans la texture");
    }
    return failures;
}

/// 30 images de glisser, dont chaque taille est nouvelle, puis une taille stable : au plus 2
/// textures nées (la première, puis celle de la taille stable), et la seconde ne naît qu'à la
/// deuxième image stable. En attendant, l'ancienne est rendue et étirée.
int dragRecreatesOnce(const levain::tests::TestBackend& backend)
{
    constexpr int DragFrames = 30;
    constexpr SceneSize Final{.width = 640, .height = 360};
    const Run run = play(
        backend,
        [Final](int frame, bool) -> std::optional<SceneSize>
        {
            if (frame < DragFrames)
            {
                const auto step = static_cast<std::uint32_t>(frame);
                return SceneSize{.width = 300 + (7 * step), .height = 200 + (3 * step)};
            }
            return Final;
        },
        DragFrames + 6);
    // La première taille du glissé est celle de la première texture ; la taille stable est reprise
    // à l'image DragFrames + (StableFramesBeforeResize - 1).
    constexpr SceneSize First{.width = 300, .height = 200};
    constexpr int Settled = DragFrames + levain::app::StableFramesBeforeResize - 1;
    int failures = expect(run.exitCode == 0, "glisser : code 0") +
                   expect(run.rendered.size() == DragFrames + 6, "glisser : une scène par image");
    failures += expect(run.created <= 2, "glisser : au plus 2 textures nées");
    failures +=
        expect(run.created == 2, "glisser : la taille stable donne bien une seconde texture");
    failures +=
        expect(run.registeredTextures == 1,
               "glisser : l'ancienne image est libérée après l'image, seule la neuve reste");
    // « Libérée après l'image » : à la recréation, l'ancienne image est encore dans la table quand
    // `requestScene` rend la main, et un dessin de cette image la montre (`drawStale`, que
    // l'assertion de `recordUi` garde en Debug) ; libérée tout de suite, elle n'y serait plus.
    failures += expect(run.replacements == 1 && run.replacedStillRegistered == 1,
                       "glisser : l'ancienne image reste dans la table jusqu'à la fin de l'image");
    for (const Rendered& rendered : run.rendered)
    {
        const SceneSize wanted = rendered.frame < Settled ? First : Final;
        failures += expect(sameSize(rendered, wanted) && aspectFollows(rendered),
                           std::format("glisser, image {} : rendue à {} × {}", rendered.frame,
                                       wanted.width, wanted.height));
    }
    // Pendant le glissé, la Vue reçoit toujours une image, la même (étirée) : jamais rien.
    failures += expect(std::ranges::all_of(run.asked, [](const Asked& a) { return a.gotImage; }),
                       "glisser : la Vue reçoit une image à chaque image");
    return failures;
}

/// Une Vue fermée (0 × 0) : aucune scène n'est rendue, mais les images se jouent jusqu'au bout (la
/// boucle ne s'arrête pas) et la swapchain est effacée, rien d'autre. Ouverte trois images avant,
/// la texture existe : elle ne doit pas être rendue ni montrée.
int closedViewRendersNothing(const levain::tests::TestBackend& backend)
{
    constexpr int OpenFrames = 3;
    constexpr int Frames = 8;
    const Run run = play(
        backend, [](int frame, bool) -> std::optional<SceneSize>
        { return frame < OpenFrames ? SceneSize{.width = 640, .height = 360} : SceneSize{}; },
        Frames, {}, true);
    int failures = expect(run.exitCode == 0, "Vue fermée : les 8 images se jouent, code 0");
    failures += expect(run.rendered.size() == OpenFrames,
                       std::format("Vue fermée : {} scènes rendues (celles de la Vue ouverte), pas "
                                   "plus ; en a rendu {}",
                                   OpenFrames, run.rendered.size()));
    failures += expect(std::ranges::none_of(run.asked, [](const Asked& a)
                                            { return a.size.empty() && a.gotImage; }),
                       "Vue fermée : aucune image n'est donnée à la Vue");
    // La capture est l'image présentée de la dernière image : la swapchain effacée, sans scène.
    if (!run.capture)
    {
        return failures + expect(false, "Vue fermée : la capture est relue");
    }
    failures += expect(isClear(pixelOf(*run.capture, InsideX, InsideY)) &&
                           isClear(pixelOf(*run.capture, OutsideX, OutsideY)),
                       "Vue fermée : la swapchain est effacée, la scène d'avant n'y reste pas");
    return failures;
}

/// Une Vue ouverte : la scène est dans la texture, montrée par l'image au bon endroit, avec la
/// couleur de la texture (convertie une seule fois), et la swapchain effacée partout ailleurs.
int swapchainClearedAroundTheView(const levain::tests::TestBackend& backend)
{
    const Run run = play(
        backend, [](int, bool) { return SceneSize{.width = 640, .height = 360}; }, 4, {}, true);
    if (!run.capture || run.exitCode != 0)
    {
        return expect(false, "image montrée : code 0 et la capture relue");
    }
    const Pixel inside = pixelOf(*run.capture, InsideX, InsideY);
    const Pixel outside = pixelOf(*run.capture, OutsideX, OutsideY);
    int failures = 0;
    failures += expect(isScene(inside), "image montrée : la scène est visible dans la Vue");
    failures +=
        expect(std::ranges::all_of(std::array<std::size_t, 3>{0, 1, 2},
                                   [&](std::size_t channel)
                                   {
                                       return std::abs(inside.channel(channel) -
                                                       run.textureCenter.channel(channel)) <= 2;
                                   }),
               "image montrée : la couleur est celle de la texture, convertie une fois");
    failures +=
        expect(isClear(outside), "image montrée : la swapchain est effacée autour de l'image");
    return failures;
}

/// F1 (ADR-0036) : panneaux fermés, la Vue ne demande rien et la scène remplit la fenêtre, comme
/// avant ; panneaux ouverts, elle va dans la texture de la Vue. Le centre de l'espace d'ancrage ne
/// laisse passer l'image que dans le premier cas : la première image des panneaux le lit encore
/// ouvert (la Vue ne demande que dans `ui`, après lui), les suivantes non.
int panelsToggleTheOutput(const levain::tests::TestBackend& backend)
{
    constexpr SceneSize View{.width = 400, .height = 100};
    const Run run = play(
        backend, [View](int, bool panelsOpen) -> std::optional<SceneSize>
        { return panelsOpen ? std::optional{View} : std::nullopt; }, 10,
        "2 key down F1\n3 key up F1\n6 key down F1\n7 key up F1\n");
    int failures = expect(run.exitCode == 0, "F1 : code 0") +
                   expect(run.rendered.size() == 10, "F1 : une scène par image");
    // Panneaux ouverts de l'image 2 à la 5 : 400 × 100 ; fermés avant et après : la fenêtre.
    for (const Rendered& rendered : run.rendered)
    {
        const bool open = rendered.frame >= 2 && rendered.frame <= 5;
        const bool right = open
                               ? sameSize(rendered, View)
                               : (rendered.width == WindowWidth && rendered.height == WindowHeight);
        failures += expect(right && aspectFollows(rendered),
                           std::format("F1, image {} : la scène est dans {}", rendered.frame,
                                       open ? "la texture de la Vue" : "la fenêtre"));
    }
    failures += expect(run.created == 1, "F1 : une seule texture, créée à la première demande");
    for (const Asked& asked : run.asked)
    {
        // Image 2 : la scène était dans la fenêtre à l'image d'avant ; 3 à 5 : dans la texture.
        failures += expect(asked.dockspace,
                           std::format("F1, image {} : les panneaux sont là", asked.frame));
        failures += expect(asked.passthru == (asked.frame == 2),
                           std::format("F1, image {} : le centre ne laisse passer l'image que "
                                       "tant que la scène est dans la fenêtre",
                                       asked.frame));
    }
    failures +=
        expect(run.asked.size() == 4, "F1 : la Vue demande quand les panneaux sont ouverts");
    return failures;
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
            std::println(stderr, "usage : levain_scene_view [vulkan|d3d12|d3d12-warp]");
            return 2;
        }
        const int failures = renderedAtRequestedSize(*backend) + dragRecreatesOnce(*backend) +
                             closedViewRendersNothing(*backend) +
                             swapchainClearedAroundTheView(*backend) +
                             panelsToggleTheOutput(*backend);
        return failures == 0 ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
