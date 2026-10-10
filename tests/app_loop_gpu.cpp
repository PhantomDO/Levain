// La boucle de l'App vue de ses points nommés (ADR-0036, morceau 4) : la simulation à l'arrêt et
// l'horloge des squelettes, dans la vraie boucle (`runApp`), hors écran. Chaque scénario écrit ce
// qu'un programme (demain, l'éditeur) ferait par les points d'accroche, et relit dans `record` ce
// que le rendu va voir. Il faut un device, comme `levain_app_script`.
//   levain_app_loop [vulkan|d3d12|d3d12-warp]

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <functional>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <glm/glm.hpp>

#include "gpu_test_backend.hpp"

#include "levain/app/app.hpp"
#include "levain/app/load_model.hpp"
#include "levain/assets/asset_ref.hpp"
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
    float alpha = 0.0f;            ///< `RenderAlpha`
    float accumulator = 0.0f;      ///< celui de `FixedStep`
    std::vector<glm::mat4> pose{}; ///< la pose du modèle skinné, s'il y en a un
};

/// Ce qu'un scénario pose : la scène, et ce que ses points d'accroche font à chaque image.
struct Scenario
{
    bool paused = false;
    std::optional<std::filesystem::path> assetDir; ///< une racine d'assets de plus, avec le modèle
    std::optional<double> frozenSeconds;           ///< `--time`
    std::function<void(App&, int frame)> onFrame;  ///< `hooks.frame`
    std::function<void(App&, int frame)> onUi;     ///< `hooks.ui`
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
    if (!app.models.empty())
    {
        seen.pose = app.models.begin()->second.pose.joints;
    }
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
        if (scenario.assetDir)
        {
            const auto loaded =
                levain::app::loadModel(app, {.path = *scenario.assetDir / "two-joints.gltf",
                                             .placement = {},
                                             .name = "modele",
                                             .clip = "tourne",
                                             .locomotion = std::nullopt});
            if (!loaded)
            {
                return std::unexpected(loaded.error());
            }
        }
        levain::app::FrameHooks hooks;
        hooks.frame = [&scenario](App& frameApp)
        {
            if (scenario.onFrame)
            {
                scenario.onFrame(frameApp, frameApp.frameCount);
            }
        };
        hooks.ui = [&scenario](App& uiApp)
        {
            if (scenario.onUi)
            {
                scenario.onUi(uiApp, uiApp.frameCount);
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
    if (scenario.assetDir)
    {
        settings.assetRoots.push_back(*scenario.assetDir);
    }
    settings.bindingsFile = LEVAIN_DATA_DIR "/input.cfg";
    settings.api = backend.api;
    settings.adapter = backend.adapter;
    settings.frozenSeconds = scenario.frozenSeconds;
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

/// Un dossier à soi, effacé à la fin du test : le scan des assets y écrit ses `.meta`, qu'il ne
/// faut pas laisser dans le dépôt.
struct TempDir
{
    std::filesystem::path path;

    TempDir()
        : path{std::filesystem::temp_directory_path() /
               ("levain-app-loop-" + levain::assets::toString(levain::assets::generateAssetId()))}
    {
        std::filesystem::create_directories(path);
    }

    ~TempDir()
    {
        std::error_code ignored; // un destructeur ne lève pas : le dossier reste dans /tmp
        std::filesystem::remove_all(path, ignored);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

/// Le modèle skinné des tests (`two-joints.gltf`, un clip de rotation de 1 s en boucle), copié dans
/// un dossier à soi : le scan des assets y écrirait ses `.meta` à côté, donc dans le dépôt.
void copyModelInto(const TempDir& dir)
{
    std::filesystem::copy_file(std::filesystem::path{LEVAIN_TEST_DATA_DIR} / "two-joints.gltf",
                               dir.path / "two-joints.gltf");
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

/// La pose du squelette à l'image `frame`, ou une erreur franche si la boucle n'a pas joué
/// jusque-là.
const std::vector<glm::mat4>& poseAt(const Run& run, int frame)
{
    return run.seen.at(static_cast<std::size_t>(frame)).pose;
}

/// Un scénario où le modèle skinné est chargé et où chaque image dure 20 ms au moins (`sleep_for`)
/// : le temps de la scène avance d'une image à l'autre, celui des squelettes seulement à l'arrêt
/// non.
Scenario skeletonScenario(const TempDir& dir)
{
    Scenario scenario;
    scenario.assetDir = dir.path;
    scenario.onFrame = [](App&, int)
    { std::this_thread::sleep_for(std::chrono::milliseconds{20}); };
    return scenario;
}

/// La pose d'un squelette ne bouge pas pendant l'arrêt, et bouge avant et après.
int playSkeleton(const levain::tests::TestBackend& backend, const TempDir& dir)
{
    constexpr int PauseFrame = 10;
    constexpr int ResumeSkeletonFrame = 30;
    Scenario scenario = skeletonScenario(dir);
    scenario.onFrame = [](App& app, int frame)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
        app.simulationPaused = frame >= PauseFrame && frame < ResumeSkeletonFrame;
    };
    const Run run = play(backend, scenario, ResumeSkeletonFrame + 5);

    bool movesBefore = !poseAt(run, 0).empty();
    for (int frame = 1; frame < PauseFrame; ++frame)
    {
        movesBefore = movesBefore && poseAt(run, frame) != poseAt(run, frame - 1);
    }
    bool frozen = true;
    for (int frame = PauseFrame; frame < ResumeSkeletonFrame; ++frame)
    {
        frozen = frozen && poseAt(run, frame) == poseAt(run, PauseFrame - 1);
    }
    bool movesAfter = true;
    for (int frame = ResumeSkeletonFrame; frame < ResumeSkeletonFrame + 5; ++frame)
    {
        movesAfter = movesAfter && poseAt(run, frame) != poseAt(run, frame - 1);
    }
    return expect(run.exitCode == 0, "le modèle skinné se charge, la boucle va à son terme") +
           expect(movesBefore, "avant l'arrêt, la pose du squelette change à chaque image") +
           expect(frozen, "à l'arrêt, la pose du squelette ne bouge pas") +
           expect(movesAfter, "au retour au jeu, la pose repart");
}

/// Un arrêt posé par `ui` (le bouton Lecture / Arrêt de l'éditeur) vaut pour la simulation et les
/// squelettes à partir de l'image suivante : l'image où `ui` le pose a joué son pas avec l'ancien
/// état, et ses squelettes aussi. Les deux lisent le drapeau une fois, au même moment.
int playPauseFromUi(const levain::tests::TestBackend& backend, const TempDir& dir)
{
    constexpr int ClickFrame = 8;
    Scenario scenario = skeletonScenario(dir);
    scenario.onUi = [](App& app, int frame)
    {
        if (frame == ClickFrame)
        {
            app.simulationPaused = true;
        }
    };
    const Run run = play(backend, scenario, ClickFrame + 4);
    if (expect(run.exitCode == 0 && run.seen.size() == ClickFrame + 4, "l'arrêt posé par ui") != 0)
    {
        return 1;
    }
    const auto moverAt = [&run](int frame)
    { return run.seen.at(static_cast<std::size_t>(frame)).moverX; };
    return expect(
               moverAt(ClickFrame) > moverAt(ClickFrame - 1) &&
                   moverAt(ClickFrame + 1) == moverAt(ClickFrame),
               "l'arrêt posé dans ui : la simulation joue encore cette image, plus la suivante") +
           expect(poseAt(run, ClickFrame) != poseAt(run, ClickFrame - 1) &&
                      poseAt(run, ClickFrame + 1) == poseAt(run, ClickFrame) &&
                      poseAt(run, ClickFrame + 3) == poseAt(run, ClickFrame),
                  "l'arrêt posé dans ui : les squelettes suivent la simulation, au même moment");
}

/// `--time` fige les squelettes à T, **démarrée à l'arrêt comme non** (l'éditeur s'ouvre en
/// Édition) : à l'arrêt dès la première image, la pose est celle de T, non celle de 0.
int playSkeletonFrozenTime(const levain::tests::TestBackend& backend, const TempDir& dir)
{
    constexpr double Quarter =
        0.25; // le clip tourne en 1 s : la pose à 0,25 s n'est pas celle de 0
    Scenario running = skeletonScenario(dir);
    running.frozenSeconds = Quarter;
    Scenario stopped = running;
    stopped.paused = true;
    Scenario origin = running;
    origin.frozenSeconds = 0.0;
    const Run atQuarter = play(backend, running, 4);
    const Run stoppedAtQuarter = play(backend, stopped, 4);
    const Run atOrigin = play(backend, origin, 4);
    if (expect(atQuarter.exitCode == 0 && stoppedAtQuarter.exitCode == 0 &&
                   atOrigin.exitCode == 0 && stoppedAtQuarter.seen.size() == 4 &&
                   atQuarter.seen.size() == 4 && atOrigin.seen.size() == 4,
               "--time, trois boucles de quatre images") != 0)
    {
        return 1;
    }
    bool sameAsRunning = true;
    for (int frame = 0; frame < 4; ++frame)
    {
        sameAsRunning = sameAsRunning && poseAt(stoppedAtQuarter, frame) == poseAt(atQuarter, 0);
    }
    return expect(!poseAt(atQuarter, 0).empty() && poseAt(atQuarter, 0) != poseAt(atOrigin, 0),
                  "--time 0,25 et --time 0 donnent deux poses (le test n'est pas vide)") +
           expect(sameAsRunning,
                  "--time fige les squelettes à T, même démarrée à l'arrêt (non à 0)");
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
        const TempDir modelDir; // le modèle skinné, copié une fois pour les trois scénarios
        copyModelInto(modelDir);
        const int failures = playPaused(*backend) + playSkeleton(*backend, modelDir) +
                             playPauseFromUi(*backend, modelDir) +
                             playSkeletonFrozenTime(*backend, modelDir);
        return failures == 0 ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
