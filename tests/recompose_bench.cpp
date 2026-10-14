// Le coût de la recomposition après `ui` (ADR-0036, morceau 4) : ce que paie une image où l'éditeur
// écrit un `Transform` et demande `App::recomposeAfterUi`, sur les 10 000 cubes de la démo du
// sandbox (le critère de M2.1 : une grille de 100 × 100, enfants d'une entité « grid »).
//   levain_recompose_bench [vulkan|d3d12|d3d12-warp]
// Deux mesures. La première, sans GPU : le monde seul, une image (`advanceWorld`) et la
// recomposition (`composeWorldTransforms`). La seconde, dans la vraie boucle hors écran (`runApp`),
// sans puis avec la demande : le temps entre la fin de `ui` et le début du rendu, et l'image
// entière. Ce temps comprend l'acquisition de l'image de la swapchain (`beginFrame`), bruitée sous
// lavapipe : il est approximatif, et la mesure du monde seul est la fiable. Les cubes ne se
// dessinent pas : la recomposition ne dépend pas du dessin.
//
// Exécutable séparé, hors de `ctest`, comme `levain_scene_bench` : le chiffre dépend de la machine.
// Il se mesure en Release (`tools/recompose-cost.sh`) sur la machine de référence (SPECS §10, règle
// n°6) avant d'aller au journal.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <format>
#include <functional>
#include <print>
#include <span>
#include <stdexcept>
#include <vector>

#include <flecs.h>

#include "gpu_test_backend.hpp"

#include "levain/app/app.hpp"
#include "levain/scene/components.hpp"
#include "levain/scene/scene.hpp"

namespace
{

using Clock = std::chrono::steady_clock;
using levain::scene::Transform;

constexpr int GridSide = 100;
constexpr float GridSpacing = 1.5f;
constexpr int Frames = 400;
constexpr int WarmUpFrames = 40;

/// Les cubes de la démo, comme `spawnCubeGrid` du sandbox : enfants de « grid » par
/// `flecs::Parent`.
void spawnCubeGrid(flecs::world& world)
{
    const flecs::entity grid = world.entity("grid").set(Transform{});
    for (int z = 0; z < GridSide; ++z)
    {
        for (int x = 0; x < GridSide; ++x)
        {
            world.entity(flecs::Parent{grid}, std::format("cube_{}_{}", x, z).c_str())
                .set(Transform{.position = {static_cast<float>(x) * GridSpacing, 0.0f,
                                            static_cast<float>(z) * GridSpacing}})
                .set(levain::scene::Velocity{});
        }
    }
}

double milliseconds(Clock::duration duration)
{
    return std::chrono::duration<double, std::milli>(duration).count();
}

double median(std::vector<double> values)
{
    std::ranges::sort(values);
    return values.empty() ? 0.0 : values[values.size() / 2];
}

double medianMilliseconds(const std::function<void()>& turn)
{
    std::vector<double> samples;
    for (int frame = 0; frame < Frames + WarmUpFrames; ++frame)
    {
        const Clock::time_point start = Clock::now();
        turn();
        if (frame >= WarmUpFrames)
        {
            samples.push_back(milliseconds(Clock::now() - start));
        }
    }
    return median(std::move(samples));
}

void measureWorld()
{
    flecs::world world;
    world.import<levain::scene::SceneModule>();
    spawnCubeGrid(world);
    levain::scene::FixedStep step;
    const double image =
        medianMilliseconds([&] { levain::scene::advanceWorld(world, step, 1.0f / 144.0f); });
    const double recomposition =
        medianMilliseconds([&world] { levain::scene::composeWorldTransforms(world); });
    std::print("monde seul, {} cubes : une image {:.3f} ms, la recomposition {:.3f} ms ({:.0f} % "
               "de l'image)\n",
               GridSide * GridSide, image, recomposition, 100.0 * recomposition / image);
}

/// Ce que la vraie boucle mesure pour une image sur deux, avec ou sans la demande de recomposition.
struct LoopCost
{
    /// De la fin de `ui` au début de `record` : la recomposition en fait partie, et l'acquisition
    /// de la swapchain aussi.
    double uiToRecordMs = 0.0;
    /// D'un `record` au suivant : l'image entière, attente du GPU comprise.
    double frameMs = 0.0;
};

LoopCost measureLoop(const levain::tests::TestBackend& backend, bool recompose)
{
    std::vector<double> gaps;
    std::vector<double> frames;
    Clock::time_point uiEnd;
    Clock::time_point previousRecord;
    levain::app::AppSettings settings;
    settings.steps = Frames + WarmUpFrames;
    settings.width = 640;
    settings.height = 360;
    settings.assetRoots = {LEVAIN_DATA_DIR};
    settings.bindingsFile = LEVAIN_DATA_DIR "/input.cfg";
    settings.api = backend.api;
    settings.adapter = backend.adapter;
    const int code = levain::app::runApp(
        settings,
        [&](levain::app::App& app) -> levain::core::Result<levain::app::FrameHooks>
        {
            app.world.entity("camera")
                .set(Transform{.position = {0, 5, 20}})
                .set(levain::app::CameraLens{});
            spawnCubeGrid(app.world);
            levain::app::FrameHooks hooks;
            hooks.ui = [&](levain::app::App& uiApp)
            {
                uiApp.recomposeAfterUi = recompose;
                uiEnd = Clock::now();
            };
            hooks.record = [&](levain::app::App& recordApp, nvrhi::ICommandList&, double)
            {
                const Clock::time_point now = Clock::now();
                if (recordApp.frameCount >= WarmUpFrames)
                {
                    gaps.push_back(milliseconds(now - uiEnd));
                    frames.push_back(milliseconds(now - previousRecord));
                }
                previousRecord = now;
            };
            return hooks;
        });
    if (code != 0)
    {
        throw std::runtime_error{"la boucle a échoué"};
    }
    return {.uiToRecordMs = median(std::move(gaps)), .frameMs = median(std::move(frames))};
}

} // namespace

int main(int argc, char** argv)
try
{
    const std::span arguments{argv, static_cast<std::size_t>(argc)};
    const auto backend =
        levain::tests::testBackendNamed(arguments.size() == 2 ? arguments[1] : "vulkan");
    if (arguments.size() > 2 || !backend)
    {
        std::println(stderr, "usage : levain_recompose_bench [vulkan|d3d12|d3d12-warp]");
        return 2;
    }
    measureWorld();
    const LoopCost without = measureLoop(*backend, false);
    const LoopCost with = measureLoop(*backend, true);
    std::print("boucle, sans la demande : ui -> rendu {:.3f} ms, image {:.3f} ms\n",
               without.uiToRecordMs, without.frameMs);
    std::print("boucle, avec la demande  : ui -> rendu {:.3f} ms, image {:.3f} ms\n",
               with.uiToRecordMs, with.frameMs);
    std::print("la recomposition ajoute {:.3f} ms entre ui et le rendu (médianes de {} images ; "
               "approximatif : cet intervalle comprend l'acquisition de la swapchain, le chiffre "
               "fiable est celui du monde seul)\n",
               with.uiToRecordMs - without.uiToRecordMs, Frames);
    return 0;
}
catch (const std::exception& e)
{
    std::fputs(e.what(), stderr);
    std::fputc('\n', stderr);
    return 1;
}
