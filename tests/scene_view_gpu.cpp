// La scène dans une texture, dans la vraie boucle (ADR-0036, morceau 7). Un programme minimal joue
// la Vue : dans `ui`, il demande une taille (`requestScene`), comme le fera la fenêtre du
// morceau 8. Une fonction d'étape inscrite chez le renderer note la cible et l'aspect de chaque
// scène rendue ; la texture de la scène est relue après la boucle. Il faut un device, comme
// `levain_app_loop`.
//   levain_scene_view [vulkan|d3d12|d3d12-warp]

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <format>
#include <functional>
#include <optional>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "gpu_test_backend.hpp"

#include "levain/app/app.hpp"
#include "levain/render/readback.hpp"
#include "levain/render/stages.hpp"
#include "levain/scene/components.hpp"

namespace
{

using levain::app::App;
using levain::app::SceneSize;

/// La fenêtre du test : 320 × 180.
constexpr std::uint32_t WindowWidth = 320;
constexpr std::uint32_t WindowHeight = 180;

/// Une scène rendue : la cible de l'image HDR (sa taille est celle de la sortie du renderer) et
/// l'aspect de la projection que le renderer a construite.
struct Rendered
{
    int frame = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    float aspect = 0.0f;
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
    int created = 0;
    /// Les textures que la table de l'UI tient pour le programme à la fin : celle de la scène,
    /// et plus aucune des anciennes.
    std::ptrdiff_t registeredTextures = 0;
    SceneSize texture{}; ///< la texture de la scène à la fin de la boucle
    Pixel textureCenter;
};

/// Ce que la Vue demande à l'image `frame`, `panelsOpen` étant l'état de F1. Rien : la scène reste
/// dans la fenêtre.
using Ask = std::function<std::optional<SceneSize>(int frame, bool panelsOpen)>;

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
        hooks.ui = [&ask](App& uiApp)
        {
            const int frame = uiApp.frameCount;
            const std::optional<SceneSize> size = ask(frame, uiApp.ui.panelsOpen);
            if (!size)
            {
                return;
            }
            // L'image n'est pas montrée : la texture est relue après la boucle.
            static_cast<void>(levain::app::requestScene(uiApp, *size));
        };
        hooks.finish = [&run](App& finishApp)
        {
            readScene(finishApp, run);
            return true;
        };
        return hooks;
    };
}

/// Le programme dans la vraie boucle, hors écran, `frames` images.
Run play(const levain::tests::TestBackend& backend, const Ask& ask, int frames)
{
    levain::app::AppSettings settings;
    settings.steps = frames;
    settings.width = WindowWidth;
    settings.height = WindowHeight;
    settings.assetRoots = {LEVAIN_DATA_DIR};
    settings.bindingsFile = LEVAIN_DATA_DIR "/input.cfg";
    settings.api = backend.api;
    settings.adapter = backend.adapter;
    Run run;
    run.exitCode = levain::app::runApp(settings, programOf(run, ask));
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

/// Le fond de la scène est orange (`App::background`) : le rouge domine le bleu, ce qu'aucun gris
/// ne fait.
bool isScene(const Pixel& pixel)
{
    return pixel.channel(0) > pixel.channel(2) + 20;
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
        const int failures = renderedAtRequestedSize(*backend);
        return failures == 0 ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
