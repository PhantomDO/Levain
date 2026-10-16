// La pause du jeu, de bout en bout, dans la vraie boucle (ADR-0037, décision 6,
// morceau 5), un script d'input rejoué hors écran (le banc d'essai de l'App) :
//   - pause : la touche Pause arrête les pas, l'accumulateur ne bouge pas, le `frame` du programme
//     se tait, le jeu ne reçoit rien (une touche tenue en pause ne le rejoint pas à la reprise) ;
//     Pause reprend ; Échap, de la pause, arrête et rend la scène (la ligne `editor.stop`).
// Il faut un device, comme `levain_app_script`.
//   levain_editor_pause [vulkan|d3d12|d3d12-warp]

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
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
#include <utility>
#include <vector>

#include <imgui.h>

#include "gpu_test_backend.hpp"

#include "levain/app/app.hpp"
#include "levain/app/player_input.hpp"
#include "levain/core/log_ring.hpp"
#include "levain/editor/editor.hpp"
#include "levain/editor/mode.hpp"
#include "levain/input/state.hpp"
#include "levain/platform/input_script.hpp"
#include "levain/scene/camera_control.hpp"
#include "levain/scene/components.hpp"

namespace
{

using levain::app::App;
using levain::editor::Action;
using levain::editor::Editor;
using levain::editor::Mode;

/// Ce que le banc relève à la fin de chaque image (`afterUi`), la route comprise.
struct Frame
{
    Mode mode = Mode::Edit;
    bool paused = false;
    bool mouseFreed = false;
    int steps = 0;            ///< `App::stepsPlayed`
    float accumulator = 0.0f; ///< celui de `FixedStep`
    int programFrames = 0;    ///< les appels du `frame` du programme
    float moverX = 0.0f;      ///< ce que la simulation a avancé
    float forward = 0.0f;     ///< l'axe `move_forward` de `PlayerInput`
    bool anyHeld = false;     ///< une action tenue dans `PlayerInput`
    bool imguiW = false;      ///< ImGui voit la touche W tenue
    bool leftSeen = false;    ///< le jeu tient le bouton gauche
    bool captured = false;    ///< la souris est capturée
    levain::app::InputRoute route = levain::app::InputRoute::Ui; ///< celle de l'image suivante
    int pauses = 0;        ///< `ActionCounters::performed` de Pause
    int steppedFrames = 0; ///< de Image suivante
    int refusedSteps = 0;  ///< `declined` de Image suivante
    levain::editor::ToolbarLayout toolbar{};
    levain::app::ScreenRect view{};
    float cameraZ = 0.0f; ///< la caméra que W, tenue, fait avancer
};

struct Run
{
    int exitCode = 0;
    std::vector<Frame> frames;
    std::vector<std::string> stopLines; ///< les `editor.stop` du journal
    int programFrames = 0;
};

/// Ce qu'un scénario ajoute à une exécution.
struct Options
{
    bool panels = false;    ///< panneaux ouverts (640 x 360) ou fermés (320 x 180)
    bool grabMouse = false; ///< le `frame` du programme veut la souris à chaque image
    /// Appelé à la fin de chaque image, éditeur modifiable : un test règle la table des raccourcis
    /// ou l'accumulateur sans passer par la fenêtre.
    std::function<void(App&, Editor&, int frame)> atFrameEnd;
};

levain::app::StartFunction programOf(Run& run, bool grabMouse)
{
    return [&run, grabMouse](App& app) -> levain::core::Result<levain::app::FrameHooks>
    {
        app.world.entity("camera")
            .set(levain::scene::Transform{})
            .set(levain::app::CameraLens{})
            .set(levain::scene::FpsController{});
        const auto forward = levain::input::axisIndex(app.bindings, "move_forward");
        if (!forward)
        {
            return levain::core::makeError(levain::core::ErrorCode::InvalidData,
                                           "data/input.cfg : move_forward manque");
        }
        // Épinglée : une vraie fenêtre lirait la disposition de la machine (AZERTY chez Donnovan).
        app.keyAtPosition = levain::platform::qwertyKeyAtPosition;
        app.world.entity("mover")
            .set(levain::scene::Transform{})
            .set(levain::scene::Velocity{.linear = {1, 0, 0}});
        levain::app::FrameHooks hooks;
        hooks.frame = [&run, grabMouse, forward = *forward](App& frameApp)
        {
            ++run.programFrames;
            // Comme le sandbox : ce que le joueur demande, lu sur l'input de l'image, que les
            // systèmes du pas appliquent ; la caméra avance tant que W est tenue.
            frameApp.world.set<levain::scene::FpsInput>(
                {.move = {0.0f, levain::input::axisValue(frameApp.input, forward)}});
            frameApp.mouseCaptureWanted = grabMouse; // un jeu qui reprend la souris à chaque image
        };
        return hooks;
    };
}

Frame frameOf(App& app, const Editor& editor, const Run& run, int forwardAxis)
{
    const auto& state = app.world.get<levain::app::PlayerInput>().state;
    Frame frame;
    frame.mode = editor.mode.current;
    frame.paused = editor.mode.paused;
    frame.mouseFreed = editor.mode.mouseFreed;
    frame.steps = app.stepsPlayed;
    frame.accumulator = app.fixedStep.accumulator;
    frame.programFrames = run.programFrames;
    frame.moverX = app.world.lookup("mover").get<levain::scene::Transform>().position.x;
    frame.forward = levain::input::axisValue(state, forwardAxis);
    frame.anyHeld = std::ranges::any_of(state.actionsHeld, [](bool held) { return held; });
    frame.imguiW = ImGui::IsKeyDown(ImGuiKey_W);
    frame.leftSeen = app.input.raw.mouseButtons.test(1);
    frame.captured = app.mouseCaptured;
    frame.route = app.inputRoute;
    frame.pauses = editor.actions.performed.at(levain::editor::indexOf(Action::Pause));
    frame.steppedFrames = editor.actions.performed.at(levain::editor::indexOf(Action::NextFrame));
    frame.refusedSteps = editor.actions.declined.at(levain::editor::indexOf(Action::NextFrame));
    frame.toolbar = editor.toolbar;
    frame.view = editor.view.image;
    frame.cameraZ = app.world.lookup("camera").get<levain::scene::Transform>().position.z;
    return frame;
}

Run play(const levain::tests::TestBackend& backend, std::string_view script, int frames,
         const Options& options = {})
{
    auto parsed = levain::platform::parseInputScript(script);
    if (!parsed)
    {
        throw std::runtime_error{parsed.error().message};
    }
    levain::app::AppSettings settings;
    settings.inputScript = std::move(*parsed);
    settings.steps = frames;
    settings.showUiPanels = options.panels;
    settings.width = options.panels ? 640 : 320;
    settings.height = options.panels ? 360 : 180;
    settings.assetRoots = {LEVAIN_DATA_DIR};
    settings.bindingsFile = LEVAIN_DATA_DIR "/input.cfg";
    settings.api = backend.api;
    settings.adapter = backend.adapter;
    Run run;
    const levain::core::LogCapture capture = levain::core::captureLog(1024);
    levain::editor::EditorOptions editorOptions;
    editorOptions.fontRoots = {{LEVAIN_TEST_ASSETS_DIR}};
    App* appSeen = nullptr;
    int forwardAxis = -1;
    editorOptions.afterUi = [&](const Editor& editor)
    {
        if (appSeen != nullptr && forwardAxis >= 0)
        {
            run.frames.push_back(frameOf(*appSeen, editor, run, forwardAxis));
        }
    };
    editorOptions.afterUiMutable = [&](Editor& editor)
    {
        if (appSeen != nullptr && options.atFrameEnd)
        {
            options.atFrameEnd(*appSeen, editor, static_cast<int>(run.frames.size()) - 1);
        }
    };
    auto start = programOf(run, options.grabMouse);
    run.exitCode = levain::app::runApp(
        settings,
        levain::editor::withEditor(
            [&](App& app) -> levain::core::Result<levain::app::FrameHooks>
            {
                appSeen = &app;
                const auto axis = levain::input::axisIndex(app.bindings, "move_forward");
                if (!axis)
                {
                    return levain::core::makeError(levain::core::ErrorCode::InvalidData,
                                                   "data/input.cfg : move_forward manque");
                }
                forwardAxis = *axis;
                return start(app);
            },
            std::move(editorOptions)));
    for (const auto& entry : levain::core::snapshotOf(*capture.ring).entries)
    {
        if (entry.text.starts_with("editor.stop"))
        {
            run.stopLines.push_back(entry.text);
        }
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

/// Le nombre qui commence le texte, 0 s'il n'y en a pas.
int numberAt(std::string_view text)
{
    int value = 0;
    static_cast<void>(std::from_chars(text.data(), text.data() + text.size(), value));
    return value;
}

/// Le script, ses lignes rangées par numéro d'image (`parseInputScript` refuse un numéro qui
/// recule) : un test assemble ses morceaux sans compter, l'ordre des lignes d'une même image est
/// gardé.
std::string sorted(const std::string& script)
{
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < script.size())
    {
        const std::size_t end = script.find('\n', start);
        lines.push_back(script.substr(start, end - start));
        start = end == std::string::npos ? script.size() : end + 1;
    }
    std::ranges::stable_sort(lines, {}, [](const std::string& line) { return numberAt(line); });
    std::string joined;
    for (const std::string& line : lines)
    {
        joined += line + "\n";
    }
    return joined;
}

/// Une touche pressée à l'image `frame` et relâchée à la suivante.
std::string tap(int frame, std::string_view key)
{
    return std::format("{0} key down {1}\n{2} key up {1}\n", frame, key, frame + 1);
}

std::string altP(int frame)
{
    return std::format("{0} key down Left Alt\n{0} key down P\n{1} key up P\n{1} key up Left Alt\n",
                       frame, frame + 1);
}

/// Le nombre dans « steps=120 » d'une ligne `editor.stop`, ou -1.
int stepsOf(const std::string& line)
{
    constexpr std::string_view Key = "steps=";
    const std::size_t at = line.find(Key);
    return at == std::string::npos ? -1 : numberAt(std::string_view{line}.substr(at + Key.size()));
}

bool noDifferences(const std::string& line)
{
    return line.find(" differences=0 ") != std::string::npos;
}

/// Les pas joués par l'image `frame` seule.
int stepsIn(const Run& run, std::size_t frame)
{
    return run.frames.at(frame).steps - (frame > 0 ? run.frames.at(frame - 1).steps : 0);
}

/// Un reste dans l'accumulateur, posé en jeu : c'est ce que la pause ne doit ni vider ni remplir.
constexpr float Leftover = 0.4f;

void plantAccumulator(App& app, Editor&, int frame)
{
    if (frame == 6)
    {
        app.fixedStep.accumulator = Leftover * app.fixedStep.stepSeconds;
    }
}

/// La pause de bout en bout, par la touche. Alt+P à l'image 2 (le jeu dès la 3), Pause à l'image
/// 10 (arrêté dès la 10, la touche étant lue dans `frame`, avant le monde), W tenue de 14 à 24 et D
/// tenue de 18 à 40 (elle traverse la reprise), Pause de nouveau à l'image 30 (reprise dès la 30),
/// Échap à l'image 45.
int pauseAndResume(const levain::tests::TestBackend& backend)
{
    const std::string script =
        sorted(altP(2) + tap(10, "Pause") + "14 key down W\n24 key up W\n" +
               "18 key down D\n40 key up D\n" + tap(30, "Pause") + tap(45, "Escape"));
    Options options;
    options.atFrameEnd = plantAccumulator;
    constexpr int Frames = 60;
    const Run run = play(backend, script, Frames, options);
    if (expect(run.exitCode == 0 && run.frames.size() == Frames,
               "pause : la boucle va au bout, une image relevée par image") != 0)
    {
        return 1;
    }
    const float step = levain::scene::FixedStep{}.stepSeconds;
    int failures = 0;
    // Le jeu joue de l'image 3 à la 9 : un pas par image.
    for (std::size_t frame = 3; frame < 10; ++frame)
    {
        failures += expect(stepsIn(run, frame) == 1 && !run.frames.at(frame).paused,
                           std::format("pause, image {} : un pas par image avant la pause", frame));
    }
    const float accumulatorAtPause = run.frames.at(9).accumulator;
    failures += expect(std::abs(accumulatorAtPause - Leftover * step) < step * 1e-3f,
                       "pause : le reste planté dans l'accumulateur est là avant la pause");
    // Pause à l'image 10, lue dans `frame` : ni pas, ni `frame` du programme, de la 10 à la 29.
    const int programFramesAtPause = run.frames.at(9).programFrames;
    for (std::size_t frame = 10; frame < 30; ++frame)
    {
        const Frame& seen = run.frames.at(frame);
        const std::string at = std::format("pause, image {} : ", frame);
        failures += expect(seen.paused && seen.mode == Mode::Play, at + "en pause, en jeu");
        failures += expect(stepsIn(run, frame) == 0, at + "aucun pas");
        failures += expect(seen.accumulator == accumulatorAtPause,
                           at + "l'accumulateur n'a pas bougé, au bit près");
        failures += expect(seen.programFrames == programFramesAtPause,
                           at + "le frame du programme se tait");
        failures += expect(seen.moverX == run.frames.at(9).moverX, at + "la scène ne bouge pas");
        // Le jeu ne reçoit rien : la route est celle de l'éditeur, ImGui a le clavier (W, 14 à 24).
        failures += expect(seen.route == levain::app::InputRoute::Editor,
                           at + "la route est celle de l'éditeur");
        failures += expect(seen.forward == 0.0f && !seen.anyHeld,
                           at + "PlayerInput reste vide, touches tenues");
        failures += expect(seen.imguiW == (frame >= 14 && frame < 24),
                           at + "ImGui voit W en pause, le jeu non");
    }
    failures += expect(run.frames.at(29).pauses == 1, "pause : la touche est comptée une fois");
    // Pause à l'image 30 : reprise. Un pas par image, le `frame` du programme revient, et D, tenue
    // depuis la pause, n'est pas vue par le jeu : il ne la voit qu'à un nouvel appui.
    for (std::size_t frame = 30; frame < 45; ++frame)
    {
        const Frame& seen = run.frames.at(frame);
        const std::string at = std::format("reprise, image {} : ", frame);
        failures += expect(!seen.paused && stepsIn(run, frame) == 1, at + "un pas par image");
        failures += expect(seen.programFrames == run.frames.at(frame - 1).programFrames + 1,
                           at + "le frame du programme revient");
        failures += expect(!seen.anyHeld && seen.forward == 0.0f,
                           at + "D, tenue pendant la pause, n'est pas vue par le jeu");
        failures += expect(std::abs(seen.accumulator - accumulatorAtPause) < step * 1e-3f,
                           at + "l'accumulateur est intact (à l'arrondi des pas près)");
    }
    failures += expect(run.frames.at(44).pauses == 2, "reprise : la seconde touche est comptée");
    // Échap, à l'image 45, de la reprise : l'Édition, la scène rendue.
    failures += expect(run.frames.at(45).mode == Mode::Edit && !run.frames.at(45).paused,
                       "Échap arrête le jeu, la pause tombe");
    failures += expect(run.stopLines.size() == 1 && noDifferences(run.stopLines.front()) &&
                           stepsOf(run.stopLines.front()) > 0,
                       "Échap : la ligne editor.stop, des pas, zéro différence");
    return failures;
}

/// Échap, **de la pause** : l'arrêt, la scène rendue, la pause qui tombe. Un jeu en pause ne reçoit
/// rien, et c'est pourtant lui qu'Échap arrêtait.
int escapeFromPause(const levain::tests::TestBackend& backend)
{
    const Run run = play(
        backend, altP(2) + tap(8, "Pause") + tap(14, "Escape") + altP(20) + tap(24, "Pause"), 30);
    if (expect(run.exitCode == 0 && run.frames.size() == 30, "Échap en pause : code 0") != 0)
    {
        return 1;
    }
    int failures = 0;
    failures += expect(run.frames.at(13).paused && stepsIn(run, 13) == 0,
                       "Échap en pause : la pause tient jusqu'à l'image 13");
    failures += expect(run.frames.at(14).mode == Mode::Edit && !run.frames.at(14).paused,
                       "Échap en pause : l'Édition, la pause tombée");
    failures += expect(!run.stopLines.empty() && noDifferences(run.stopLines.front()) &&
                           stepsOf(run.stopLines.front()) == run.frames.at(8).steps,
                       "Échap en pause : Stop, zéro différence, les pas joués avant la pause");
    // Une session neuve ne reprend pas la pause de la précédente : Alt+P (20), un pas par image.
    failures += expect(!run.frames.at(21).paused && stepsIn(run, 21) == 1,
                       "Jouer après une pause : la session repart en marche");
    failures += expect(run.frames.at(24).paused && stepsIn(run, 24) == 0,
                       "la pause d'une nouvelle session marche aussi");
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
            std::println(stderr, "usage : levain_editor_pause [vulkan|d3d12|d3d12-warp]");
            return 2;
        }
        const int failures = pauseAndResume(*backend) + escapeFromPause(*backend);
        return failures == 0 ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
