// Pause, Image suivante et la teinte du jeu dans la vraie boucle (ADR-0037, décision 6,
// morceau 5), un script d'input rejoué hors écran (le banc d'essai de l'App) :
//   - pause : la touche Pause arrête les pas, l'accumulateur ne bouge pas, le `frame` du programme
//     se tait, le jeu ne reçoit rien (une touche tenue en pause ne le rejoint pas à la reprise) ;
//     Pause reprend ; Échap, de la pause, arrête et rend la scène (la ligne `editor.stop`) ;
//   - Image suivante : exactement un pas, puis la pause reprend ; refusée en marche ;
//   - la barre d'outils : Pause, Image suivante, Reprendre et Arrêter, cliqués à la souris
//     scriptée ;
//   - la teinte : la barre d'outils et le bord de la Vue prennent la couleur du jeu, une autre en
//     pause, aucune en Édition (lues dans les listes de dessin d'ImGui, image par image).
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
#include <imgui_internal.h>

#include "gpu_test_backend.hpp"

#include "levain/app/app.hpp"
#include "levain/app/player_input.hpp"
#include "levain/core/log_ring.hpp"
#include "levain/editor/editor.hpp"
#include "levain/editor/mode.hpp"
#include "levain/editor/shortcut_edit.hpp"
#include "levain/input/state.hpp"
#include "levain/platform/input_script.hpp"
#include "levain/scene/camera_control.hpp"
#include "levain/scene/components.hpp"
#include "levain/ui/style.hpp"
#include "levain/ui/tr.hpp"

namespace
{

using levain::app::App;
using levain::editor::Action;
using levain::editor::Editor;
using levain::editor::Mode;

constexpr std::uint16_t ScanN = 17; // USB HID, vérifié contre SDL

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

/// Un clic de la souris scriptée : le curseur d'abord (ImGui trouve ce qu'il survole avant
/// l'appui), l'appui à l'image d'après, le relâchement à la suivante.
std::string click(int frame, ImVec2 at)
{
    return std::format("{0} mouse move {1} {2}\n{3} button down left\n{4} button up left\n", frame,
                       at.x, at.y, frame + 1, frame + 2);
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

/// Donne un accord à « Image suivante » (aucun par défaut) comme la fenêtre « Raccourcis » le fait,
/// par `replaceChord`.
void bindNextFrame(Editor& editor, const levain::editor::Chord& chord)
{
    levain::editor::replaceChord(editor.shortcuts, Action::NextFrame, 0, chord,
                                 levain::platform::qwertyKeyAtPosition);
}

/// Image suivante : exactement un pas, puis la pause reprend. L'accord est celui qu'on lui donne
/// (position de N). Alt+P à 2, N à 5 (en marche : refusée, aucun pas de plus), Pause à 8, N à 12 et
/// à 20 (un pas chacun), N tenue de 26 à 40 sans se répéter (un seul pas), Pause à 44 (reprise).
int stepOnce(const levain::tests::TestBackend& backend)
{
    // N tenue de 26 à 40, avec les répétitions de SDL (des appuis de plus, sans relâchement).
    const std::string script =
        sorted(altP(2) + tap(5, "N") + tap(8, "Pause") + tap(12, "N") + tap(20, "N") +
               "26 key down N\n28 key down N\n30 key down N\n40 key up N\n" + tap(44, "Pause"));
    Options options;
    options.atFrameEnd = [](App& app, Editor& editor, int frame)
    {
        if (frame == 0)
        {
            bindNextFrame(editor, levain::editor::positionChord(ScanN));
        }
        plantAccumulator(app, editor, frame);
    };
    constexpr int Frames = 50;
    const Run run = play(backend, script, Frames, options);
    if (expect(run.exitCode == 0 && run.frames.size() == Frames,
               "Image suivante : la boucle va au bout") != 0)
    {
        return 1;
    }
    int failures = 0;
    // En marche, à l'image 5 : un pas comme les autres, la demande refusée et comptée.
    failures += expect(stepsIn(run, 5) == 1 && run.frames.at(5).refusedSteps == 1 &&
                           run.frames.at(5).steppedFrames == 0,
                       "Image suivante en marche : refusée, comptée, aucun pas de plus");
    // De la pause (8) à la reprise (44), des pas aux images 12, 20 et 26 seulement, un chacun.
    for (std::size_t frame = 8; frame < 44; ++frame)
    {
        const bool stepped = frame == 12 || frame == 20 || frame == 26;
        const Frame& seen = run.frames.at(frame);
        const std::string at = std::format("Image suivante, image {} : ", frame);
        failures +=
            expect(stepsIn(run, frame) == (stepped ? 1 : 0),
                   at + (stepped ? "exactement un pas" : "aucun pas, puis la pause reprend"));
        failures += expect(seen.paused && seen.accumulator == run.frames.at(8).accumulator,
                           at + "en pause, l'accumulateur intact, pas compris");
        // Le `frame` du programme tourne pour un pas, et pour lui seul (sous l'input vide de la
        // pause) : le pas lit ce qu'il pose, non l'input d'avant la pause.
        const int stepsSincePause =
            (frame >= 12 ? 1 : 0) + (frame >= 20 ? 1 : 0) + (frame >= 26 ? 1 : 0);
        failures += expect(seen.programFrames == run.frames.at(8).programFrames + stepsSincePause,
                           at + "le frame du programme tourne pour un pas, pas en pause");
    }
    failures += expect(run.frames.at(43).steppedFrames == 3,
                       "Image suivante : trois pas comptés (le tenu ne se répète pas)");
    failures += expect(run.frames.at(43).refusedSteps == 1, "Image suivante : un seul refus");
    failures += expect(run.frames.at(12).moverX > run.frames.at(11).moverX,
                       "Image suivante : le pas joué se voit dans la scène");
    // La reprise (44) : un pas par image de nouveau.
    failures += expect(!run.frames.at(45).paused && stepsIn(run, 45) == 1 && stepsIn(run, 49) == 1,
                       "Reprendre après des pas : un pas par image");
    return failures;
}

/// **Image suivante ne rejoue pas l'input d'avant la pause.** W est tenue (images 6 à 13) et la
/// caméra avance ; Pause à 10 ; W relâchée à 14, en pause ; deux pas, à 18 et 22 : la caméra ne
/// bouge pas, car le `frame` du programme y pose l'input de la pause, vide (`FpsInput` du sandbox
/// restait sinon sur « avant », et chaque pas avançait la caméra). Le jeu réclame la souris à
/// chaque image (`grabMouse`) : même pour un pas, elle reste à l'éditeur.
int stepsDoNotReplayInput(const levain::tests::TestBackend& backend)
{
    Options options;
    options.grabMouse = true;
    options.atFrameEnd = [](App&, Editor& editor, int frame)
    {
        if (frame == 0)
        {
            bindNextFrame(editor, levain::editor::positionChord(ScanN));
        }
    };
    const std::string script = sorted(altP(2) + "6 key down W\n14 key up W\n" + tap(10, "Pause") +
                                      tap(18, "N") + tap(22, "N"));
    constexpr int Frames = 28; // le script va jusqu'à l'image 23
    const Run run = play(backend, script, Frames, options);
    if (expect(run.exitCode == 0 && run.frames.size() == Frames,
               "pas sans input rejoué : code 0") != 0)
    {
        return 1;
    }
    const float movingTo = run.frames.at(9).cameraZ;
    int failures =
        expect(movingTo < run.frames.at(5).cameraZ && run.frames.at(8).captured,
               "pas sans input rejoué : la caméra avance tant que W est tenue en jeu, la "
               "souris est au jeu (la précondition)");
    for (std::size_t frame = 10; frame < Frames; ++frame)
    {
        const Frame& seen = run.frames.at(frame);
        const std::string at = std::format("pas sans input rejoué, image {} : ", frame);
        failures += expect(seen.cameraZ == movingTo, at + "la caméra ne bouge pas, pas compris");
        failures += expect(!seen.captured, at + "la souris reste à l'éditeur, même pour un pas");
        failures += expect(seen.forward == 0.0f && !seen.anyHeld, at + "PlayerInput reste vide");
    }
    failures += expect(run.frames.at(18).moverX > run.frames.at(17).moverX &&
                           run.frames.at(22).moverX > run.frames.at(21).moverX,
                       "pas sans input rejoué : les deux pas ont bien joué");
    failures += expect(run.frames.at(18).programFrames == run.frames.at(17).programFrames + 1 &&
                           run.frames.at(22).programFrames == run.frames.at(21).programFrames + 1,
                       "pas sans input rejoué : le frame du programme a tourné à chaque pas");
    return failures;
}

/// Un pas demandé par la barre d'outils (dans `ui`, relâché à l'image 10) et Échap à l'image
/// d'après, avant que le pas ne soit joué : Stop l'emporte. Sans cela l'Édition jouerait le pas
/// après que la scène a été rendue, et ce que le jeu a déplacé le serait de nouveau.
int stopAfterStepRequest(const levain::tests::TestBackend& backend, ImVec2 stepPaused,
                         const Options& options)
{
    const Run run =
        play(backend, sorted(altP(2) + tap(4, "Pause") + click(8, stepPaused) + tap(11, "Escape")),
             16, options);
    if (expect(run.exitCode == 0 && run.frames.size() == 16, "pas puis Échap : code 0") != 0)
    {
        return 1;
    }
    return expect(run.frames.at(10).steppedFrames == 1 && run.frames.at(10).paused,
                  "pas puis Échap : le pas est demandé en pause") +
           expect(run.frames.at(11).mode == Mode::Edit && stepsIn(run, 11) == 0 &&
                      stepsIn(run, 12) == 0,
                  "pas puis Échap : Stop l'emporte, l'Édition ne joue pas le pas demandé") +
           expect(run.stopLines.size() == 1 && noDifferences(run.stopLines.front()),
                  "pas puis Échap : la scène est rendue, zéro différence");
}

/// La barre d'outils, panneaux ouverts, cliquée à la souris scriptée : Pause (image 5 et 6), Image
/// suivante (10 et 11), Reprendre (15 et 16). Un passage sans clic relève où sont les boutons, en
/// marche puis en pause (leur largeur change avec le libellé).
int toolbarButtons(const levain::tests::TestBackend& backend)
{
    Options options;
    options.panels = true;
    const Run probe =
        play(backend, sorted(altP(2) + tap(6, "Pause") + "0 mouse move 1 1\n"), 12, options);
    if (expect(probe.frames.size() == 12 && probe.frames.at(4).toolbar.pause.x > 0.0f &&
                   probe.frames.at(10).toolbar.step.x > 0.0f,
               "la barre d'outils : le passage qui relève les boutons les a vus") != 0)
    {
        return 1;
    }
    const ImVec2 pauseRunning = probe.frames.at(4).toolbar.pause;
    const ImVec2 pausePaused = probe.frames.at(10).toolbar.pause;
    const ImVec2 stepPaused = probe.frames.at(10).toolbar.step;
    // Pause (relâché à l'image 6), Image suivante (11), Reprendre (16).
    const std::string script =
        sorted(altP(2) + click(4, pauseRunning) + click(9, stepPaused) + click(14, pausePaused));
    constexpr int Frames = 22;
    const Run run = play(backend, script, Frames, options);
    if (expect(run.exitCode == 0 && run.frames.size() == Frames, "la barre d'outils : code 0") != 0)
    {
        return 1;
    }
    int failures = 0;
    // `ui` voit le relâchement et met en pause après le monde de l'image : l'arrêt vaut dès la 7.
    failures += expect(stepsIn(run, 5) == 1 && !run.frames.at(5).paused,
                       "la barre : avant le relâchement, le jeu joue encore");
    failures +=
        expect(stepsIn(run, 6) == 1 && run.frames.at(6).paused && run.frames.at(6).pauses == 1,
               "la barre : le bouton Pause met en pause, à la fin de l'image");
    for (std::size_t frame = 7; frame < 16; ++frame)
    {
        const bool stepped = frame == 12; // Image suivante relâché à 11 : un pas à l'image 12
        failures += expect(stepsIn(run, frame) == (stepped ? 1 : 0),
                           std::format("la barre, image {} : {}", frame,
                                       stepped ? "Image suivante joue un pas" : "la pause tient"));
    }
    failures +=
        expect(run.frames.at(16).steppedFrames == 1, "la barre : Image suivante comptée une fois");
    failures += expect(!run.frames.at(16).paused && run.frames.at(16).pauses == 2,
                       "la barre : Reprendre reprend");
    failures += expect(stepsIn(run, 17) == 1 && stepsIn(run, 21) == 1,
                       "la barre : un pas par image après Reprendre");
    // Aucun clic de la barre n'a atteint le jeu.
    failures += expect(std::ranges::none_of(run.frames, [](const Frame& f) { return f.leftSeen; }),
                       "la barre : aucun clic de ses boutons n'atteint le jeu");
    return failures + stopAfterStepRequest(backend, stepPaused, options);
}

// -- La teinte ------------------------------------------------------------------------------------

/// La teinte qu'une fenêtre a dessinée à l'image : celle du jeu, celle de la pause, ou aucune.
enum class Tint : std::uint8_t
{
    None,
    Play,
    Paused,
};

bool sameRgb(ImU32 a, ImU32 b)
{
    return (a & ~static_cast<ImU32>(IM_COL32_A_MASK)) == (b & ~static_cast<ImU32>(IM_COL32_A_MASK));
}

/// La couleur dessinée dans la liste de dessin de la fenêtre (ses sommets, tels qu'ImGui les écrira
/// dans l'image) : ce que le rendu va montrer, sans lire l'image.
bool drawnIn(const std::string& windowName, const ImVec4& color)
{
    const ImGuiWindow* window = ImGui::FindWindowByName(windowName.c_str());
    const ImU32 wanted = ImGui::ColorConvertFloat4ToU32(color);
    return window != nullptr &&
           std::ranges::any_of(window->DrawList->VtxBuffer, [wanted](const ImDrawVert& vertex)
                               { return sameRgb(vertex.col, wanted); });
}

/// Le fond de la barre d'outils et le bord de la Vue, quelle teinte ils ont à cette image.
struct TintsDrawn
{
    Tint toolbar = Tint::None;
    Tint border = Tint::None;
};

TintsDrawn tintsDrawn()
{
    const auto of = [](const std::string& window, const ImVec4& play, const ImVec4& paused)
    {
        const bool isPlay = drawnIn(window, play);
        const bool isPaused = drawnIn(window, paused);
        return isPlay ? Tint::Play : isPaused ? Tint::Paused : Tint::None;
    };
    return {.toolbar = of(levain::ui::labelOf("Barre d'outils"),
                          levain::ui::tintedBarBackground(levain::ui::PlayTint),
                          levain::ui::tintedBarBackground(levain::ui::PausedTint)),
            .border = of(levain::ui::labelOf(levain::editor::ViewWindow), levain::ui::PlayTint,
                         levain::ui::PausedTint)};
}

/// La teinte de la barre d'outils et du bord de la Vue, image par image, dans la vraie boucle :
/// aucune en Édition, celle du jeu quand il tourne (Alt+P à 2, Pause à 8, reprise à 14), celle de
/// la pause arrêté, aucune de nouveau à Échap (20). **Lue dans les listes de dessin d'ImGui**, non
/// dans l'image : la capture de la boucle est prise après le `finish` du programme, où Stop a déjà
/// rendu l'Édition (ADR-0037), donc elle ne montrerait jamais le jeu. Le test des contrastes de
/// `ui_style_test` garde ce que la couleur donne à l'écran.
int tint(const levain::tests::TestBackend& backend)
{
    Options options;
    options.panels = true;

    struct Drawn
    {
        std::vector<TintsDrawn> frames;
    } drawn;

    options.atFrameEnd = [&drawn](App&, Editor&, int) { drawn.frames.push_back(tintsDrawn()); };
    const Run run =
        play(backend, sorted(altP(2) + tap(8, "Pause") + tap(14, "Pause") + tap(20, "Escape")), 26,
             options);
    if (expect(run.exitCode == 0 && run.frames.size() == 26 && drawn.frames.size() == 26,
               "teinte : code 0, une image relevée par image") != 0)
    {
        return 1;
    }
    int failures = 0;
    for (std::size_t frame = 0; frame < drawn.frames.size(); ++frame)
    {
        const bool inPlay = (frame >= 3 && frame < 8) || (frame >= 14 && frame < 20);
        const bool inPause = frame >= 8 && frame < 14;
        const Tint expected = inPause ? Tint::Paused : inPlay ? Tint::Play : Tint::None;
        failures += expect(drawn.frames.at(frame).toolbar == expected,
                           std::format("teinte, image {} : la barre d'outils", frame));
        failures += expect(drawn.frames.at(frame).border == expected,
                           std::format("teinte, image {} : le bord de la Vue", frame));
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
            std::println(stderr, "usage : levain_editor_pause [vulkan|d3d12|d3d12-warp]");
            return 2;
        }
        const int failures = pauseAndResume(*backend) + escapeFromPause(*backend) +
                             stepOnce(*backend) + stepsDoNotReplayInput(*backend) +
                             toolbarButtons(*backend) + tint(*backend);
        return failures == 0 ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
