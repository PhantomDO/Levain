// Le temps réel dans la vraie boucle (ADR-0036, décision 9, morceau 12), hors écran :
//   - `App::realTime` faux (« Temps réel » décoché) fige le temps de la scène que lit `record`
//     (l'eau, l'herbe et les matériaux), vrai de nouveau il repart sans saut.
// Les menus et les barres de l'éditeur, qui le règlent, s'y ajoutent ensuite.
// Il faut un device, comme `levain_app_script`.
//   levain_editor_menus [vulkan|d3d12|d3d12-warp]

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <print>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

#include "gpu_test_backend.hpp"

#include "levain/app/app.hpp"
#include "levain/scene/components.hpp"

namespace
{

using levain::app::App;

int expect(bool holds, std::string_view what)
{
    if (!holds)
    {
        std::println(stderr, "échec : {}", what);
    }
    return holds ? 0 : 1;
}

levain::app::AppSettings settingsOf(const levain::tests::TestBackend& backend, int frames)
{
    levain::app::AppSettings settings;
    settings.steps = frames;
    settings.width = 640;
    settings.height = 360;
    settings.assetRoots = {LEVAIN_DATA_DIR};
    settings.bindingsFile = LEVAIN_DATA_DIR "/input.cfg";
    settings.api = backend.api;
    settings.adapter = backend.adapter;
    return settings;
}

constexpr int RealTimeFrames = 30;
constexpr int FreezeFrom = 8;
constexpr int FreezeUntil = 20;
constexpr double FrozenTolerance = 1.0e-6;
constexpr std::chrono::milliseconds FrozenFrameSleep{50}; // 12 images gelées : 0,6 s

/// « Temps réel » décoché de l'image `FreezeFrom` à `FreezeUntil` (exclue) : le temps de la scène
/// que `record` reçoit ne bouge pas, puis repart de là sans sauter du temps gelé.
int playRealTime(const levain::tests::TestBackend& backend)
{
    std::vector<double> seconds;
    int frame = 0;
    const auto start = [&](App& app) -> levain::core::Result<levain::app::FrameHooks>
    {
        app.world.entity("camera").set(levain::scene::Transform{}).set(levain::app::CameraLens{});
        levain::app::FrameHooks hooks;
        hooks.frame = [&frame](App& frameApp)
        {
            frameApp.realTime = !(frame >= FreezeFrom && frame < FreezeUntil);
            if (!frameApp.realTime)
            {
                // Du temps réel passe pendant le gel : sans lui, « sans saut » ne se distinguerait
                // pas d'un gel qui rattrape.
                std::this_thread::sleep_for(FrozenFrameSleep);
            }
        };
        hooks.record = [&seconds, &frame](App&, nvrhi::ICommandList&, double scene)
        {
            seconds.push_back(scene);
            ++frame;
        };
        return hooks;
    };
    if (levain::app::runApp(settingsOf(backend, RealTimeFrames), start) != 0 ||
        seconds.size() != RealTimeFrames)
    {
        return expect(false, "le temps réel : code 0 et une image enregistrée par image");
    }
    const auto at = [&seconds](int index) { return seconds.at(static_cast<std::size_t>(index)); };
    bool frozen = true;
    for (int i = FreezeFrom; i < FreezeUntil; ++i)
    {
        // À l'arrondi près : le temps gelé est une différence de doubles.
        frozen = frozen && std::abs(at(i) - at(FreezeFrom)) < FrozenTolerance;
    }
    return expect(at(FreezeFrom - 1) > at(0), "avant le gel, le temps de la scène court") +
           expect(frozen, "décoché, le temps de la scène que lit record est figé") +
           expect(at(FreezeUntil) > at(FreezeUntil - 1) && at(FreezeUntil + 5) > at(FreezeUntil),
                  "recoché, il repart") +
           // Sans saut : le temps gelé n'est pas rattrapé (l'horloge compte le temps passé gelé).
           // 0,3 s : la moitié du temps gelé (0,6 s) ; un rattrapage le dépasserait, une image
           // lente d'une machine chargée n'y atteint pas.
           expect(at(FreezeUntil) - at(FreezeFrom) < 0.3,
                  "recoché, il repart d'où il s'était arrêté, sans rattraper le temps gelé");
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
            std::println(stderr, "usage : levain_editor_menus [vulkan|d3d12|d3d12-warp]");
            return 2;
        }
        const int failures = playRealTime(*backend);
        return failures == 0 ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
