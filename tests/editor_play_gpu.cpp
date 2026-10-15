// Jouer et Arrêter dans la vraie boucle (ADR-0037, morceau 4), un script d'input rejoué hors écran
// (le banc d'essai de l'App) :
//   - le critère : Alt+P, 120 pas, Échap, et la scène est identique (la ligne `editor.stop`), deux
//     fois de suite, les corps retombant au bit près de la même façon ;
//   - l'Édition après Stop est celle d'avant le premier Play : aucun corps de Jolt, aucun handle,
//     rien que le rayon d'un clic puisse toucher, les racines de la Hiérarchie dans le même ordre.
// Il faut un device, comme `levain_app_script`.
//   levain_editor_play [vulkan|d3d12|d3d12-warp]

#include <bit>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <functional>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <flecs.h>

#include "gpu_test_backend.hpp"

#include "levain/app/app.hpp"
#include "levain/assets/asset_id.hpp"
#include "levain/core/log_ring.hpp"
#include "levain/editor/editor.hpp"
#include "levain/editor/mode.hpp"
#include "levain/physics/physics.hpp"
#include "levain/physics/physics_world.hpp"
#include "levain/physics/queries.hpp"
#include "levain/platform/input_script.hpp"
#include "levain/scene/components.hpp"

namespace
{

using levain::app::App;
using levain::editor::Editor;
using levain::editor::Mode;
using levain::scene::Transform;

constexpr int CrateCount = 6;

/// Ce que l'éditeur et le monde sont, à la fin de chaque image.
struct Frame
{
    Mode mode = Mode::Edit;
    bool playing = false; ///< `Editor::play` est plein
    int sessionSteps = 0; ///< les pas joués depuis le début de cette session
    std::vector<Transform> crates;
    std::uint32_t bodies = 0;           ///< les corps du monde Jolt
    std::int32_t handles = 0;           ///< les entités qui portent un `BodyHandle`
    bool rayHit = false;                ///< un rayon tiré d'en haut sur la pile touche un corps
    std::vector<flecs::entity_t> roots; ///< les lignes de la Hiérarchie
    std::int32_t transforms = 0;
    std::size_t deadCrates = 0; ///< les caisses détruites en jeu et pas (encore) revenues
};

struct Run
{
    int exitCode = 0;
    std::vector<Frame> frames;
    std::vector<std::string> stopLines; ///< les `editor.stop` du journal
    std::vector<flecs::entity_t> crateIds;
};

struct Scenario
{
    std::string script;
    int frames = 0;
    /// Appelé par le `frame` du programme, en jeu seulement.
    std::function<void(App&, int programFrame)> inProgramFrame;
};

/// Un dossier à soi, effacé à la fin : le scan des assets y écrit ses `.meta`.
struct TempDir
{
    std::filesystem::path path;

    TempDir()
        : path{
              std::filesystem::temp_directory_path() /
              ("levain-editor-play-" + levain::assets::toString(levain::assets::generateAssetId()))}
    {
        std::filesystem::create_directories(path);
    }

    ~TempDir()
    {
        std::error_code ignored; // un destructeur ne lève pas
        std::filesystem::remove_all(path, ignored);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

bool sameBits(const Transform& a, const Transform& b)
{
    const auto bits = [](float value) { return std::bit_cast<std::uint32_t>(value); };
    return bits(a.position.x) == bits(b.position.x) && bits(a.position.y) == bits(b.position.y) &&
           bits(a.position.z) == bits(b.position.z) && bits(a.rotation.x) == bits(b.rotation.x) &&
           bits(a.rotation.y) == bits(b.rotation.y) && bits(a.rotation.z) == bits(b.rotation.z) &&
           bits(a.rotation.w) == bits(b.rotation.w);
}

bool sameBits(const std::vector<Transform>& a, const std::vector<Transform>& b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if (!sameBits(a[i], b[i]))
        {
            return false;
        }
    }
    return true;
}

int expect(bool holds, std::string_view what)
{
    if (!holds)
    {
        std::println(stderr, "échec : {}", what);
    }
    return holds ? 0 : 1;
}

/// Un sol et une pile de caisses lâchées en désordre, la vue physics en petit.
void dropCrates(App& app, Run& run)
{
    app.world.entity("sol")
        .set(Transform{.position = {0.0f, -0.5f, 0.0f}})
        .set(levain::physics::Collider{.shape = levain::physics::Box{{50.0f, 0.5f, 50.0f}}});
    for (int i = 0; i < CrateCount; ++i)
    {
        const float jitter = 0.07f * static_cast<float>(i % 5);
        run.crateIds.push_back(
            app.world.entity(std::format("caisse_{}", i).c_str())
                .set(Transform{.position = {jitter, 0.6f + 1.1f * static_cast<float>(i), -jitter}})
                .set(levain::physics::Collider{.shape = levain::physics::Box{{0.5f, 0.5f, 0.5f}}})
                .set(levain::physics::RigidBody{})
                .id());
    }
    // Trois racines simples, sans corps : celle du milieu détruite en jeu ne revient en fin de
    // table que si la Hiérarchie ne range pas ses racines par index (les caisses, elles, changent
    // toutes de table à Stop et reviennent dans l'ordre de l'instantané).
    for (int i = 0; i < 3; ++i)
    {
        app.world.entity(std::format("simple_{}", i).c_str()).set(Transform{});
    }
}

Frame frameOf(App& app, const Editor& editor, const Run& run)
{
    Frame frame;
    frame.mode = editor.mode.current;
    frame.playing = editor.play.has_value();
    frame.sessionSteps = editor.play ? app.stepsPlayed - editor.play->stepsAtStart : 0;
    for (const flecs::entity_t id : run.crateIds)
    {
        const bool alive = app.world.is_alive(id);
        frame.deadCrates += alive ? 0 : 1;
        frame.crates.push_back(alive ? app.world.entity(id).get<Transform>() : Transform{});
    }
    const auto& physics = app.world.get<levain::physics::PhysicsWorld>();
    frame.bodies = levain::physics::bodyCount(physics);
    frame.handles = app.world.count<levain::physics::BodyHandle>();
    frame.rayHit = levain::physics::raycast(physics, {.origin = {0.0f, 50.0f, 0.0f},
                                                      .direction = {0.0f, -1.0f, 0.0f},
                                                      .maxDistance = 100.0f})
                       .has_value();
    for (const levain::editor::HierarchyRow& row : editor.hierarchy.rows)
    {
        frame.roots.push_back(row.entity);
    }
    frame.transforms = app.world.count<Transform>();
    return frame;
}

levain::app::StartFunction programOf(Run& run, const Scenario& scenario)
{
    return [&run, &scenario](App& app) -> levain::core::Result<levain::app::FrameHooks>
    {
        app.world.import<levain::physics::PhysicsModule>();
        app.world.entity("camera")
            .set(Transform{.position = {0.0f, 3.0f, 10.0f}})
            .set(levain::app::CameraLens{});
        // Épinglée : une vraie fenêtre lirait la disposition de la machine (AZERTY chez Donnovan).
        app.keyAtPosition = levain::platform::qwertyKeyAtPosition;
        dropCrates(app, run);
        levain::app::FrameHooks hooks;
        hooks.frame = [&scenario, programFrame = 0](App& frameApp) mutable
        {
            if (scenario.inProgramFrame)
            {
                scenario.inProgramFrame(frameApp, programFrame);
            }
            ++programFrame;
        };
        return hooks;
    };
}

Run play(const levain::tests::TestBackend& backend, const Scenario& scenario, const TempDir& dir)
{
    auto parsed = levain::platform::parseInputScript(scenario.script);
    if (!parsed)
    {
        throw std::runtime_error{parsed.error().message};
    }
    levain::app::AppSettings settings;
    settings.inputScript = std::move(*parsed);
    settings.steps = scenario.frames;
    settings.showUiPanels = true;
    settings.width = 640;
    settings.height = 360;
    settings.assetRoots = {LEVAIN_DATA_DIR, dir.path};
    settings.bindingsFile = LEVAIN_DATA_DIR "/input.cfg";
    settings.api = backend.api;
    settings.adapter = backend.adapter;
    Run run;
    const levain::core::LogCapture capture = levain::core::captureLog(1024);
    levain::editor::EditorOptions options;
    options.fontRoots = {{LEVAIN_TEST_ASSETS_DIR}};
    // Le pointeur de l'App est pris au démarrage du programme : `afterUi` ne reçoit que l'éditeur.
    App* appSeen = nullptr;
    options.afterUi = [&](const Editor& editor)
    {
        if (appSeen == nullptr)
        {
            return;
        }
        run.frames.push_back(frameOf(*appSeen, editor, run));
    };
    auto start = programOf(run, scenario);
    run.exitCode = levain::app::runApp(
        settings, levain::editor::withEditor(
                      [&](App& app) -> levain::core::Result<levain::app::FrameHooks>
                      {
                          appSeen = &app;
                          return start(app);
                      },
                      std::move(options)));
    for (const auto& entry : levain::core::snapshotOf(*capture.ring).entries)
    {
        if (entry.text.starts_with("editor.stop"))
        {
            run.stopLines.push_back(entry.text);
        }
    }
    return run;
}

std::string altP(int frame)
{
    return std::format("{0} key down Left Alt\n{0} key down P\n{1} key up P\n{1} key up Left Alt\n",
                       frame, frame + 1);
}

std::string escape(int frame)
{
    return std::format("{} key down Escape\n{} key up Escape\n", frame, frame + 1);
}

/// Le nombre dans « steps=120 » d'une ligne `editor.stop`, ou -1.
int stepsOf(const std::string& line)
{
    constexpr std::string_view Key = "steps=";
    const std::size_t at = line.find(Key);
    if (at == std::string::npos)
    {
        return -1;
    }
    const char* const begin = line.data() + at + Key.size();
    int steps = -1;
    std::from_chars(begin, line.data() + line.size(), steps);
    return steps;
}

bool noDifferences(const std::string& line)
{
    return line.find(" differences=0 ") != std::string::npos;
}

/// Le critère de M7.5 dans l'éditeur : deux sessions de 120 pas, la scène identique après chacune.
/// Alt+P à l'image 2 : le jeu dès la 3. Échap à l'image 123 : 120 pas, Stop avant le 121e.
int criterion(const levain::tests::TestBackend& backend)
{
    const TempDir dir;
    Scenario scenario;
    scenario.script = altP(2) + escape(123) + altP(126) + escape(247);
    scenario.frames = 250;
    // Une RACINE du milieu de la pile détruite en jeu, au 30e pas de chaque session : Stop doit la
    // rendre à sa place dans la Hiérarchie (les racines par index), de bout en bout.
    scenario.inProgramFrame = [](App& app, int programFrame)
    {
        if (programFrame % 120 == 30)
        {
            app.world.lookup("caisse_2").destruct();
            app.world.lookup("simple_1").destruct();
        }
    };
    const Run run = play(backend, scenario, dir);
    int failures =
        expect(run.exitCode == 0, "le critère : la boucle va au bout du script, code 0") +
        expect(run.frames.size() == 250, "le critère : une image relevée par image");
    if (run.frames.size() != 250)
    {
        return failures + 1;
    }
    failures += expect(run.stopLines.size() == 2, "le critère : une ligne editor.stop par Stop");
    for (const std::string& line : run.stopLines)
    {
        failures += expect(stepsOf(line) == 120 && noDifferences(line),
                           "le critère : « " + line + " » (120 pas, zéro différence)");
    }
    const Frame& before = run.frames.at(1);
    failures += expect(before.mode == Mode::Edit && !before.playing && before.bodies == 0 &&
                           before.handles == 0 && !before.rayHit,
                       "l'Édition d'avant Play : aucun corps, aucun handle, rien à viser");
    const Frame& playing = run.frames.at(100);
    failures += expect(playing.playing && playing.bodies > 0 && playing.rayHit,
                       "en jeu : des corps, et le rayon du picking touche la pile");
    failures += expect(!sameBits(playing.crates, before.crates), "en jeu, les caisses tombent");
    failures += expect(playing.deadCrates == 1 && before.deadCrates == 0,
                       "en jeu, une caisse racine est détruite (30e pas)");

    // À chaque Stop : l'Édition d'avant, au bit près, et rien de Jolt.
    for (const std::size_t after :
         {std::size_t{124}, std::size_t{125}, std::size_t{248}, std::size_t{249}})
    {
        const Frame& frame = run.frames.at(after);
        const std::string at = std::format("image {} : ", after);
        failures += expect(frame.mode == Mode::Edit && !frame.playing, at + "revenu en Édition");
        failures += expect(sameBits(frame.crates, before.crates), at + "les caisses sont rendues");
        failures +=
            expect(frame.bodies == 0 && frame.handles == 0 && !frame.rayHit,
                   at + "aucun corps de Jolt, aucun handle, rien à viser (comme avant Play)");
        failures += expect(frame.deadCrates == 0, at + "la racine détruite en jeu est revenue");
        failures += expect(frame.roots == before.roots && frame.transforms == before.transforms,
                           at + "les racines de la Hiérarchie dans le même ordre, sans croissance");
    }

    // Jouer deux fois donne le même jeu : mêmes `Transform`, au bit près, aux mêmes pas.
    std::vector<const Frame*> first;
    std::vector<const Frame*> second;
    for (const Frame& frame : run.frames)
    {
        if (frame.playing && frame.sessionSteps > 0)
        {
            (first.size() < 120 ? first : second).push_back(&frame);
        }
    }
    failures +=
        expect(first.size() == 120 && second.size() >= 120, "deux sessions de 120 pas relevées");
    for (std::size_t i = 0; i < std::min(first.size(), second.size()); ++i)
    {
        if (!sameBits(first[i]->crates, second[i]->crates))
        {
            failures +=
                expect(false, std::format("le pas {} rejoué donne les mêmes Transform", i + 1));
            break;
        }
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
            std::println(stderr, "usage : levain_editor_play [vulkan|d3d12|d3d12-warp]");
            return 2;
        }
        const int failures = criterion(*backend);
        return failures == 0 ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
