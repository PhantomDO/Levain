// La grille au sol, le menu « Afficher » et la touche G dans la vraie boucle (ADR-0036, décisions
// 13 et 15, morceau 17) : `withEditor` autour d'un programme minimal, un script d'input rejoué hors
// écran, la disposition épinglée (QWERTY, 1280 x 800, panneaux ouverts, aucune préférence : une
// exécution scriptée n'en lit ni n'en écrit). **Ce que la grille a dessiné est observé** :
// `GridState::framesDrawn` et `lastLines` sont ceux de l'étape de rendu. La validation du device
// est exigée (Debug), et le test échoue au moindre message d'erreur du journal. Un scénario par
// exécution, quatre lancements de l'application au plus :
//   key     G bascule la vue de jeu dans la Vue ; avec une autre fenêtre focalisée et survolée, G
//           ne fait rien ; la Vue survolée la reprend
//   play    Jouer cache les aides, G ne fait rien en jeu ; revenir à l'Édition les rend
//   levain_editor_grid <vulkan|d3d12|d3d12-warp> <scénario>

#include <algorithm>
#include <array>
#include <cstdio>
#include <exception>
#include <format>
#include <mutex>
#include <optional>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include "gpu_test_backend.hpp"

#include "levain/app/app.hpp"
#include "levain/core/log.hpp"
#include "levain/editor/editor.hpp"
#include "levain/editor/grid.hpp"
#include "levain/platform/input_script.hpp"
#include "levain/scene/components.hpp"

namespace
{

using levain::app::App;
using levain::editor::Action;
using levain::editor::Editor;

constexpr int WindowWidth = 1280;
constexpr int WindowHeight = 800;
/// Un point au milieu de la Vue (panneaux ouverts, 1280 x 800), et un point dans la hiérarchie.
constexpr float ViewX = 640.0f;
constexpr float ViewY = 420.0f;
constexpr float HierarchyX = 60.0f;
constexpr float HierarchyY = 300.0f;

/// Ce que l'éditeur a fait à une image, relevé à la fin de `ui`. Le dessin de la grille est celui
/// des images **d'avant** : le rendu suit `ui`.
struct Seen
{
    bool grid = false;     ///< « Grille » est cochée
    bool gameView = false; ///< « Vue de jeu » est cochée
    bool helpers = false;  ///< `App::editorHelpersVisible`, tel que `ui` l'a laissé
    int framesDrawn = 0;   ///< les images où la grille est partie
    int lastLines = 0;     ///< ses lignes à la dernière image rendue
    int gameViewPerformed = 0;
    bool viewFocused = false;
    bool viewHovered = false;
    levain::editor::Mode mode = levain::editor::Mode::Edit;
};

struct Run
{
    int exitCode = 0;
    std::vector<Seen> seen;
    std::vector<std::string> errors; ///< les messages d'erreur du journal, du démarrage à la fin
    App* app = nullptr;
};

Run play(const levain::tests::TestBackend& backend, std::string_view script, int frames)
{
    auto parsed = levain::platform::parseInputScript(script);
    if (!parsed)
    {
        throw std::runtime_error{parsed.error().message};
    }
    levain::app::AppSettings settings;
    settings.inputScript = std::move(*parsed);
    settings.steps = frames;
    settings.showUiPanels = true;
    settings.width = WindowWidth;
    settings.height = WindowHeight;
    settings.assetRoots = {LEVAIN_DATA_DIR};
    settings.bindingsFile = LEVAIN_DATA_DIR "/input.cfg";
    settings.api = backend.api;
    settings.adapter = backend.adapter;
    Run run;
    std::mutex errorsMutex; // les messages de Jolt et du chargeur partent d'autres fils
    const levain::core::LogSinkHandle sink = levain::core::addLogSink(
        [&run, &errorsMutex](const levain::core::LogRecord& record)
        {
            if (record.level >= levain::core::LogLevel::Error)
            {
                const std::scoped_lock lock{errorsMutex};
                run.errors.push_back(std::string{record.category} + " : " +
                                     std::string{record.message});
            }
        });
    levain::editor::EditorOptions options;
    // Les polices de l'éditeur (morceau 17a) : celles d'assets-cache, que `data/` seul n'a pas.
    options.fontRoots = {{LEVAIN_TEST_ASSETS_DIR}};
    options.afterUi = [&run](const Editor& editor)
    {
        run.seen.push_back({.grid = editor.view.show.grid,
                            .gameView = editor.view.show.gameView,
                            .helpers = run.app->editorHelpersVisible,
                            .framesDrawn = editor.grid.framesDrawn,
                            .lastLines = editor.grid.lastLines,
                            .gameViewPerformed = editor.actions.performed.at(
                                levain::editor::indexOf(Action::GameView)),
                            .viewFocused = editor.view.focused,
                            .viewHovered = editor.view.hovered,
                            .mode = editor.mode.current});
    };
    const auto program = [&run](App& app) -> levain::core::Result<levain::app::FrameHooks>
    {
        // L'éditeur part de la caméra du jeu : 3 m de haut, penchée de 20 degrés vers le sol, de
        // quoi voir la grille et son bord lointain.
        app.world.entity("camera")
            .set(levain::scene::Transform{
                .position = {0.0f, 3.0f, 8.0f},
                .rotation = glm::angleAxis(glm::radians(-20.0f), glm::vec3{1.0f, 0.0f, 0.0f})})
            .set(levain::app::CameraLens{});
        app.keyAtPosition = levain::platform::qwertyKeyAtPosition; // épinglé
        run.app = &app;
        return levain::app::FrameHooks{};
    };
    run.exitCode = levain::app::runApp(settings, levain::editor::withEditor(program, options));
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

/// Une exécution qui va au bout, une image relevée par image, et aucune erreur au journal (la
/// validation du device en fait partie).
int expectClean(const Run& run, int frames)
{
    for (const std::string& error : run.errors)
    {
        std::println(stderr, "erreur au journal : {}", error);
    }
    return expect(run.exitCode == 0, "l'exécution va au bout, code 0") +
           expect(run.seen.size() == static_cast<std::size_t>(frames),
                  "une image relevée par image du script") +
           expect(run.errors.empty(), "aucune erreur au journal (validation comprise)");
}

/// Un clic à `at` : la souris y va à l'image `frame`, le bouton s'enfonce deux images plus tard.
std::string clickScript(int frame, ImVec2 at)
{
    return std::format("{} mouse move {} {}\n{} button down left\n{} button up left\n", frame, at.x,
                       at.y, frame + 2, frame + 3);
}

std::string tapKey(int frame, std::string_view key)
{
    return std::format("{} key down {}\n{} key up {}\n", frame, key, frame + 1, key);
}

std::string moveTo(int frame, float x, float y)
{
    return std::format("{} mouse move {} {}\n", frame, x, y);
}

/// Les lignes (tronçons) que la grille envoie : au moins une, jamais plus que son plafond.
constexpr int MaxGridLines = static_cast<int>(levain::editor::MaxGridLines);

/// La grille est dessinée à l'image `frame` (relevée trois images après son changement au plus tôt)
/// : ses lignes sont parties, et la compte avance encore d'une image à la suivante.
int expectDrawn(const Run& run, std::size_t frame, std::string_view at)
{
    const Seen& now = run.seen.at(frame);
    const Seen& before = run.seen.at(frame - 1);
    return expect(now.lastLines > 0 && now.lastLines <= MaxGridLines &&
                      now.framesDrawn > before.framesDrawn,
                  std::format("{} : la grille dessine ses lignes (1 à {})", at, MaxGridLines));
}

/// Elle n'est pas dessinée : l'étape a tourné sans rien envoyer, et la compte ne bouge plus.
int expectHidden(const Run& run, std::size_t frame, std::string_view at)
{
    const Seen& now = run.seen.at(frame);
    const Seen& before = run.seen.at(frame - 1);
    return expect(now.lastLines == 0 && now.framesDrawn == before.framesDrawn,
                  std::format("{} : la grille ne dessine rien", at));
}

/// Un script qui ne fait que garder la souris sur la Vue, sans clic ni touche.
std::string idleOnView()
{
    return moveTo(0, ViewX, ViewY);
}

/// **La touche G** : dans la Vue elle bascule la vue de jeu (la grille s'éteint, le drapeau tombe)
/// et la bascule encore ; avec la hiérarchie focalisée et survolée elle ne fait rien ; la Vue
/// survolée la reprend sans clic.
int playKey(const levain::tests::TestBackend& backend)
{
    // 0 : la souris sur la Vue ; G à 6 (cachée) et à 16 (rendue) ;
    // 22 : clic dans la hiérarchie (focus ailleurs), G à 32 : rien ;
    // 38 : la souris revient sur la Vue (survolée, non focalisée), G à 46 : la vue de jeu.
    std::string script = idleOnView() + tapKey(6, "G") + tapKey(16, "G") +
                         clickScript(22, {HierarchyX, HierarchyY}) + tapKey(32, "G") +
                         moveTo(38, ViewX, ViewY) + tapKey(46, "G");
    constexpr int Frames = 58;
    const Run run = play(backend, script, Frames);
    int failures = expectClean(run, Frames);
    if (run.seen.size() != Frames)
    {
        return failures;
    }
    const auto at = [&run](std::size_t frame) -> const Seen& { return run.seen.at(frame); };
    failures += expect(at(5).viewHovered && !at(5).gameView, "avant G : la Vue est survolée") +
                expectDrawn(run, 5, "avant G");
    failures += expect(at(12).gameView && !at(12).helpers && at(12).gameViewPerformed == 1,
                       "G dans la Vue : la vue de jeu, les aides tombent") +
                expectHidden(run, 12, "après G");
    failures += expect(!at(20).gameView && at(20).helpers && at(20).gameViewPerformed == 2,
                       "G encore : les aides reviennent") +
                expectDrawn(run, 21, "après le second G");
    failures += expect(!at(30).viewFocused && !at(30).viewHovered,
                       "la hiérarchie a pris le focus et la souris : la Vue n'est ni focalisée ni "
                       "survolée");
    failures += expect(!at(36).gameView && at(36).helpers && at(36).gameViewPerformed == 2,
                       "G ailleurs que dans la Vue ne fait rien") +
                expectDrawn(run, 37, "G ailleurs : la grille dessine toujours");
    failures +=
        expect(at(44).viewHovered && !at(44).viewFocused, "la Vue est survolée sans le focus") +
        expect(at(52).gameView && !at(52).helpers && at(52).gameViewPerformed == 3,
               "survolée, la Vue prend G sans clic") +
        expectHidden(run, 53, "après le troisième G");
    return failures;
}

/// **Jouer** : les aides tombent (la grille ne dessine plus), G ne fait rien en jeu, Échap les
/// rend.
int playPlay(const levain::tests::TestBackend& backend)
{
    // Alt+P à 6 : le jeu ; G à 14 (en jeu : rien) ; Échap à 22 : l'Édition.
    std::string script = idleOnView() +
                         "6 key down Left Alt\n6 key down P\n7 key up P\n7 key up Left Alt\n" +
                         tapKey(14, "G") + tapKey(22, "Escape");
    constexpr int Frames = 34;
    const Run run = play(backend, script, Frames);
    int failures = expectClean(run, Frames);
    if (run.seen.size() != Frames)
    {
        return failures;
    }
    const auto at = [&run](std::size_t frame) -> const Seen& { return run.seen.at(frame); };
    failures += expect(at(5).mode == levain::editor::Mode::Edit && at(5).helpers,
                       "en Édition les aides se voient") +
                expectDrawn(run, 5, "en Édition");
    failures += expect(at(12).mode == levain::editor::Mode::PlayWithoutReturn && !at(12).helpers,
                       "en jeu : les aides tombent") +
                expectHidden(run, 12, "en jeu");
    failures += expect(at(18).mode == levain::editor::Mode::PlayWithoutReturn && !at(18).gameView &&
                           at(18).gameViewPerformed == 0,
                       "G en jeu va au jeu, pas à la vue de jeu") +
                expectHidden(run, 18, "G en jeu");
    failures += expect(at(30).mode == levain::editor::Mode::Edit && at(30).helpers,
                       "Échap : l'Édition, les aides reviennent") +
                expectDrawn(run, 31, "retour à l'Édition");
    return failures;
}

/// Un scénario par nom : `levain_editor_grid <backend> <scénario>`.
struct Scenario
{
    std::string_view name;
    int (*run)(const levain::tests::TestBackend&);
};

constexpr std::array<Scenario, 2> Scenarios{{{"key", playKey}, {"play", playPlay}}};

} // namespace

int main(int argc, char** argv)
{
    try
    {
        const std::span arguments{argv, static_cast<std::size_t>(argc)};
        const auto backend =
            arguments.size() == 3 ? levain::tests::testBackendNamed(arguments[1]) : std::nullopt;
        const auto scenario =
            arguments.size() == 3
                ? std::ranges::find(Scenarios, std::string_view{arguments[2]}, &Scenario::name)
                : Scenarios.end();
        if (!backend || scenario == Scenarios.end())
        {
            std::println(stderr, "usage : levain_editor_grid <vulkan|d3d12|d3d12-warp> <key|play>");
            return 2;
        }
        return scenario->run(*backend) == 0 ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
