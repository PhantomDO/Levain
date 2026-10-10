// La boucle de l'App vue de ses points nommés (ADR-0036, morceau 4) : la simulation à l'arrêt,
// dans la vraie boucle (`runApp`), hors écran. Chaque scénario écrit ce qu'un programme (demain,
// l'éditeur) ferait par les points d'accroche, et relit dans `record` ce que le rendu va voir. Il
// faut un device, comme `levain_app_script`.
//   levain_app_loop [vulkan|d3d12|d3d12-warp]

#include <algorithm>
#include <cstdio>
#include <exception>
#include <functional>
#include <print>
#include <span>
#include <string_view>
#include <vector>

#include "gpu_test_backend.hpp"

#include "levain/app/app.hpp"
#include "levain/scene/components.hpp"

namespace
{

using levain::app::App;
using levain::scene::Transform;

/// Ce que `record` voit de chaque image, juste avant que le rendu ne la dessine.
struct Seen
{
    /// Le `Transform` de « mover », que la simulation avance d'un pas à la fois.
    float moverX = 0.0f;
    float alpha = 0.0f;       ///< `RenderAlpha`
    float accumulator = 0.0f; ///< celui de `FixedStep`
};

/// Ce qu'un scénario pose : la scène, et ce que ses points d'accroche font à chaque image.
struct Scenario
{
    bool paused = false;
    std::function<void(App&, int frame)> onFrame; ///< `hooks.frame`
};

struct Run
{
    int exitCode = 0;
    std::vector<Seen> seen;
};

Seen seenBy(App& app)
{
    Seen seen{.alpha = app.world.get<levain::scene::RenderAlpha>().value,
              .accumulator = app.fixedStep.accumulator};
    seen.moverX = app.world.lookup("mover").get<Transform>().position.x;
    return seen;
}

levain::app::StartFunction startOf(const Scenario& scenario, std::vector<Seen>& seen)
{
    return [&scenario, &seen](App& app) -> levain::core::Result<levain::app::FrameHooks>
    {
        app.world.entity("mover")
            .set(Transform{})
            .set(levain::scene::Velocity{.linear = {1, 0, 0}});
        app.world.entity("camera")
            .set(Transform{.position = {0.0f, 0.0f, 5.0f}})
            .set(levain::app::CameraLens{});
        app.simulationPaused = scenario.paused;
        if (scenario.paused)
        {
            // Un reste dans l'accumulateur : c'est ce que l'arrêt ne doit ni vider ni remplir.
            app.fixedStep.accumulator = 0.4f * app.fixedStep.stepSeconds;
        }
        levain::app::FrameHooks hooks;
        hooks.frame = [&scenario](App& frameApp)
        {
            if (scenario.onFrame)
            {
                scenario.onFrame(frameApp, frameApp.frameCount);
            }
        };
        hooks.record = [&seen](App& recordApp, nvrhi::ICommandList&, double)
        { seen.push_back(seenBy(recordApp)); };
        return hooks;
    };
}

/// Le scénario dans la vraie boucle, hors écran, `frames` images (`--steps` : une image par pas du
/// temps de l'image, donc le résultat ne dépend pas de la machine).
Run play(const levain::tests::TestBackend& backend, const Scenario& scenario, int frames)
{
    levain::app::AppSettings settings;
    settings.steps = frames;
    settings.width = 320;
    settings.height = 180;
    settings.assetRoots = {LEVAIN_DATA_DIR};
    settings.bindingsFile = LEVAIN_DATA_DIR "/input.cfg";
    settings.api = backend.api;
    settings.adapter = backend.adapter;
    Run run;
    run.exitCode = levain::app::runApp(settings, startOf(scenario, run.seen));
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

constexpr int ResumeFrame = 300;

/// 300 images à l'arrêt : aucun pas, `RenderAlpha` à 1 et l'accumulateur intact ; puis le retour au
/// jeu joue au plus un pas à sa première image. `startApp` lit l'arrêt aussi : la première image
/// voit un `RenderAlpha` de 1 avant même sa propre passe du monde.
int playPaused(const levain::tests::TestBackend& backend)
{
    Scenario scenario;
    scenario.paused = true;
    float alphaAtStart = 0.0f;
    scenario.onFrame = [&alphaAtStart](App& app, int frame)
    {
        if (frame == 0)
        {
            alphaAtStart = app.world.get<levain::scene::RenderAlpha>().value; // posé par startApp
        }
        if (frame == ResumeFrame)
        {
            app.simulationPaused = false;
        }
    };
    const Run run = play(backend, scenario, ResumeFrame + 6);
    // Les lectures qui suivent supposent les 306 images : sans elles, le nom de l'échec est
    // celui-ci.
    if (expect(run.exitCode == 0 && run.seen.size() == ResumeFrame + 6, "300 images à l'arrêt") !=
        0)
    {
        return 1;
    }
    const float step = levain::scene::FixedStep{}.stepSeconds;
    const auto stoppedFrames = std::span{run.seen}.first(ResumeFrame);
    const bool frozen = std::ranges::all_of(
        stoppedFrames, [step](const Seen& seen)
        { return seen.moverX == 0.0f && seen.alpha == 1.0f && seen.accumulator == 0.4f * step; });
    return expect(alphaAtStart == 1.0f, "startApp lit l'arrêt : RenderAlpha à 1 avant la première "
                                        "image") +
           expect(frozen, "à l'arrêt : aucun pas, alpha 1, l'accumulateur de FixedStep inchangé") +
           expect(run.seen[ResumeFrame].moverX > 0.0f &&
                      run.seen[ResumeFrame].moverX <= step * 1.01f,
                  "la première image du retour au jeu joue au plus un pas") +
           expect(run.seen.back().moverX <= 6.0f * step * 1.01f &&
                      run.seen.back().moverX >= 6.0f * step * 0.99f,
                  "puis un pas par image");
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
            std::println(stderr, "usage : levain_app_loop [vulkan|d3d12|d3d12-warp]");
            return 2;
        }
        const int failures = playPaused(*backend);
        return failures == 0 ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
